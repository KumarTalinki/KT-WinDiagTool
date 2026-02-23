#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "PacketCaptureCollector.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <thread>
#include <chrono>
#include <vector>

namespace KTDiag
{

bool PacketCaptureCollector::RunCommand(const std::wstring& cmdLine,
                                         std::wstring& output,
                                         DWORD timeoutMs)
{
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
        return false;

    // Ensure read end is not inherited
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    // CreateProcessW needs a writable command line buffer
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(hWritePipe);

    if (!ok)
    {
        CloseHandle(hReadPipe);
        return false;
    }

    // Read output
    std::string rawOutput;
    char readBuf[4096];
    DWORD bytesRead = 0;

    while (ReadFile(hReadPipe, readBuf, sizeof(readBuf), &bytesRead, nullptr) && bytesRead > 0)
    {
        rawOutput.append(readBuf, bytesRead);
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, timeoutMs);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Convert output to wstring (netsh outputs in system codepage)
    if (!rawOutput.empty())
    {
        int wideLen = MultiByteToWideChar(CP_ACP, 0, rawOutput.c_str(),
                                           static_cast<int>(rawOutput.size()), nullptr, 0);
        if (wideLen > 0)
        {
            output.resize(wideLen);
            MultiByteToWideChar(CP_ACP, 0, rawOutput.c_str(),
                                static_cast<int>(rawOutput.size()), &output[0], wideLen);
        }
    }

    return true;
}

bool PacketCaptureCollector::Collect(const Config& config,
                                      const std::filesystem::path& outputDir,
                                      ProgressCallback cb,
                                      CancelToken cancel)
{
    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    // Check if capture already exists (e.g., from repro mode)
    std::filesystem::path existingCapture = outputDir / L"capture.etl";
    if (std::filesystem::exists(existingCapture))
    {
        auto fileSize = std::filesystem::file_size(existingCapture);
        if (fileSize > 0)
        {
            if (cb) cb(Name(), L"Packet capture already exists (repro mode), skipping", 100);
            Logger::Instance().Log(LogLevel::Info,
                L"[Packets] Capture file already exists (%llu bytes), skipping", fileSize);
            return true;
        }
    }

    const auto& pktConfig = config.GetPacketCaptureConfig();
    int durationSec = pktConfig.durationSeconds;
    if (durationSec <= 0)
        durationSec = 60;

    if (cb) cb(Name(), L"Preparing packet capture...", 0);

    // Create output subdirectory
    std::filesystem::path pktDir = outputDir;
    std::error_code ec;
    std::filesystem::create_directories(pktDir, ec);
    if (ec)
    {
        m_error = L"Failed to create output directory";
        return false;
    }

    std::filesystem::path etlPath = pktDir / L"capture.etl";

    // Build netsh trace start command
    std::wstring startCmd = L"netsh trace start capture=yes";
    startCmd += L" traceFile=\"" + etlPath.wstring() + L"\"";
    startCmd += L" maxSize=256";
    startCmd += L" overwrite=yes";
    startCmd += L" persistent=no";

    // Note: netsh trace uses its own filter syntax, not BPF.
    // If config has a filter hint, we log it but netsh filtering is limited.
    // The capture is unfiltered by default — post-capture filtering in
    // Network Monitor or Wireshark is more flexible.
    if (!pktConfig.filter.empty())
    {
        Logger::Instance().Log(LogLevel::Info,
            L"[Packets] Config filter hint: %s (netsh captures all traffic; "
            L"apply filter in Network Monitor/Wireshark)",
            pktConfig.filter.c_str());
    }

    if (cb) cb(Name(), L"Starting netsh trace...", 5);

    Logger::Instance().Log(LogLevel::Info, L"[Packets] Running: %s", startCmd.c_str());

    std::wstring startOutput;
    if (!RunCommand(startCmd, startOutput, 30000))
    {
        m_error = L"Failed to start netsh trace (CreateProcess failed)";
        Logger::Instance().Log(LogLevel::Error, L"[Packets] %s", m_error.c_str());
        return false;
    }

    // Check for errors in output
    if (startOutput.find(L"error") != std::wstring::npos ||
        startOutput.find(L"Error") != std::wstring::npos ||
        startOutput.find(L"failed") != std::wstring::npos)
    {
        // Check if a trace is already running
        if (startOutput.find(L"already") != std::wstring::npos ||
            startOutput.find(L"in progress") != std::wstring::npos)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[Packets] A trace session may already be running. Stopping it first...");

            std::wstring stopOutput;
            RunCommand(L"netsh trace stop", stopOutput, 60000);

            // Retry start
            startOutput.clear();
            if (!RunCommand(startCmd, startOutput, 30000))
            {
                m_error = L"Failed to restart netsh trace";
                return false;
            }
        }
        else
        {
            m_error = L"netsh trace start failed: " + startOutput;
            Logger::Instance().Log(LogLevel::Error, L"[Packets] %s", m_error.c_str());
            return false;
        }
    }

    Logger::Instance().Log(LogLevel::Info,
        L"[Packets] Capture started, duration: %ds", durationSec);

    if (cb) cb(Name(), L"Capturing network traffic...", 10);

    // Wait for capture duration with cancel check
    for (int i = 0; i < durationSec; ++i)
    {
        if (cancel && cancel->load())
        {
            // Always stop netsh trace on cancel
            if (cb) cb(Name(), L"Cancelling, stopping capture...", 85);
            std::wstring stopOutput;
            RunCommand(L"netsh trace stop", stopOutput, 120000);
            m_error = L"Cancelled";
            return false;
        }

        int pct = 10 + ((i * 75) / durationSec);
        if (cb) cb(Name(), L"Capturing packets... (" +
                   std::to_wstring(durationSec - i) + L"s remaining)", pct);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Stop capture
    if (cb) cb(Name(), L"Stopping capture (this may take a moment)...", 88);

    Logger::Instance().Log(LogLevel::Info, L"[Packets] Stopping netsh trace...");

    std::wstring stopOutput;
    if (!RunCommand(L"netsh trace stop", stopOutput, 120000))
    {
        m_error = L"Failed to stop netsh trace";
        Logger::Instance().Log(LogLevel::Error, L"[Packets] %s", m_error.c_str());
        return false;
    }

    Logger::Instance().Log(LogLevel::Info, L"[Packets] netsh trace stop output: %s",
                           stopOutput.c_str());

    // Verify .etl file exists
    if (std::filesystem::exists(etlPath))
    {
        auto fileSize = std::filesystem::file_size(etlPath);
        Logger::Instance().Log(LogLevel::Info,
            L"[Packets] Capture file: %s (%llu bytes)",
            etlPath.wstring().c_str(), fileSize);
    }
    else
    {
        Logger::Instance().Log(LogLevel::Warning,
            L"[Packets] Expected capture file not found: %s", etlPath.wstring().c_str());
    }

    if (cb) cb(Name(), L"Done", 100);
    return true;
}

} // namespace KTDiag
