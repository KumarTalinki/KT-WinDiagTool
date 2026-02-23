#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "PacketCaptureController.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <vector>

namespace KTDiag
{

PacketCaptureController::PacketCaptureController() = default;

PacketCaptureController::~PacketCaptureController()
{
    if (m_active)
        Stop();
}

bool PacketCaptureController::RunCommand(const std::wstring& cmdLine,
                                          std::wstring& output,
                                          DWORD timeoutMs)
{
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
        return false;

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

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

bool PacketCaptureController::Start(const Config& config,
                                     const std::filesystem::path& outputDir)
{
    if (m_active)
    {
        m_error = L"Packet capture is already active";
        return false;
    }

    m_error.clear();

    // Create output directory matching CollectorEngine's per-collector subdirectory
    // (CollectorEngine passes outputDir/packets/ to PacketCaptureCollector)
    std::filesystem::path pktDir = outputDir / L"packets";
    std::error_code ec;
    std::filesystem::create_directories(pktDir, ec);
    if (ec)
    {
        m_error = L"Failed to create output directory for packet capture";
        return false;
    }

    m_etlPath = pktDir / L"capture.etl";

    // Build netsh trace start command
    std::wstring startCmd = L"netsh trace start capture=yes";
    startCmd += L" traceFile=\"" + m_etlPath.wstring() + L"\"";
    startCmd += L" maxSize=256";
    startCmd += L" overwrite=yes";
    startCmd += L" persistent=no";

    const auto& pktConfig = config.GetPacketCaptureConfig();
    if (!pktConfig.filter.empty())
    {
        Logger::Instance().Log(LogLevel::Info,
            L"[PktCtrl] Config filter hint: %s (netsh captures all traffic; "
            L"apply filter in Network Monitor/Wireshark)",
            pktConfig.filter.c_str());
    }

    Logger::Instance().Log(LogLevel::Info, L"[PktCtrl] Running: %s", startCmd.c_str());

    std::wstring startOutput;
    if (!RunCommand(startCmd, startOutput, 30000))
    {
        m_error = L"Failed to start netsh trace (CreateProcess failed)";
        Logger::Instance().Log(LogLevel::Error, L"[PktCtrl] %s", m_error.c_str());
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
                L"[PktCtrl] A trace session may already be running. Stopping it first...");

            std::wstring stopOutput;
            RunCommand(L"netsh trace stop", stopOutput, 60000);

            // Retry start
            startOutput.clear();
            if (!RunCommand(startCmd, startOutput, 30000))
            {
                m_error = L"Failed to restart netsh trace";
                return false;
            }

            // Check again for errors
            if (startOutput.find(L"error") != std::wstring::npos ||
                startOutput.find(L"Error") != std::wstring::npos ||
                startOutput.find(L"failed") != std::wstring::npos)
            {
                m_error = L"netsh trace start failed after retry: " + startOutput;
                Logger::Instance().Log(LogLevel::Error, L"[PktCtrl] %s", m_error.c_str());
                return false;
            }
        }
        else
        {
            m_error = L"netsh trace start failed: " + startOutput;
            Logger::Instance().Log(LogLevel::Error, L"[PktCtrl] %s", m_error.c_str());
            return false;
        }
    }

    m_active = true;
    Logger::Instance().Log(LogLevel::Info, L"[PktCtrl] Packet capture started: %s",
                           m_etlPath.wstring().c_str());
    return true;
}

bool PacketCaptureController::Stop()
{
    if (!m_active)
        return false;

    Logger::Instance().Log(LogLevel::Info, L"[PktCtrl] Stopping netsh trace...");

    std::wstring stopOutput;
    if (!RunCommand(L"netsh trace stop", stopOutput, 120000))
    {
        m_error = L"Failed to stop netsh trace";
        Logger::Instance().Log(LogLevel::Error, L"[PktCtrl] %s", m_error.c_str());
        m_active = false;
        return false;
    }

    Logger::Instance().Log(LogLevel::Info, L"[PktCtrl] netsh trace stop output: %s",
                           stopOutput.c_str());

    if (std::filesystem::exists(m_etlPath))
    {
        auto fileSize = std::filesystem::file_size(m_etlPath);
        Logger::Instance().Log(LogLevel::Info,
            L"[PktCtrl] Capture file: %s (%llu bytes)",
            m_etlPath.wstring().c_str(), fileSize);
    }
    else
    {
        Logger::Instance().Log(LogLevel::Warning,
            L"[PktCtrl] Expected capture file not found: %s",
            m_etlPath.wstring().c_str());
    }

    m_active = false;
    return true;
}

} // namespace KTDiag
