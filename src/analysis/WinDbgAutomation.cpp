#include "WinDbgAutomation.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <vector>
#include <fstream>

namespace fs = std::filesystem;

namespace KTDiag
{

fs::path WinDbgAutomation::FindCdb()
{
    // 1. Search PATH
    {
        wchar_t pathBuf[32768] = {};
        DWORD len = SearchPathW(nullptr, L"cdb.exe", nullptr, 32768, pathBuf, nullptr);
        if (len > 0)
        {
            fs::path cdbPath(pathBuf);
            if (fs::exists(cdbPath))
                return cdbPath;
        }
    }

    // 2. Common install locations
    const wchar_t* candidates[] = {
        L"C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\cdb.exe",
        L"C:\\Program Files\\Windows Kits\\10\\Debuggers\\x64\\cdb.exe",
        L"C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\arm64\\cdb.exe",
    };

    for (const auto* path : candidates)
    {
        if (fs::exists(path))
            return fs::path(path);
    }

    // 3. Registry: Windows Kits installed roots
    {
        HKEY hKey;
        LONG result = RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
            0, KEY_READ, &hKey);

        if (result == ERROR_SUCCESS)
        {
            wchar_t kitsRoot[MAX_PATH] = {};
            DWORD bufSize = sizeof(kitsRoot);
            DWORD type = 0;
            result = RegQueryValueExW(hKey, L"KitsRoot10", nullptr, &type,
                                       reinterpret_cast<BYTE*>(kitsRoot), &bufSize);
            RegCloseKey(hKey);

            if (result == ERROR_SUCCESS && type == REG_SZ)
            {
                fs::path cdbPath = fs::path(kitsRoot) / L"Debuggers" / L"x64" / L"cdb.exe";
                if (fs::exists(cdbPath))
                    return cdbPath;

                // Try arm64 as fallback
                cdbPath = fs::path(kitsRoot) / L"Debuggers" / L"arm64" / L"cdb.exe";
                if (fs::exists(cdbPath))
                    return cdbPath;
            }
        }
    }

    return fs::path(); // Not found
}

bool WinDbgAutomation::RunCommand(const std::wstring& cmdLine,
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

    // Check if process is still running (hung analysis)
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    if (exitCode == STILL_ACTIVE)
    {
        TerminateProcess(pi.hProcess, 1);
        Logger::Instance().Log(LogLevel::Warning,
            L"[WinDbg] Process timed out after %lu ms, terminated", timeoutMs);
    }

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

bool WinDbgAutomation::Analyze(const fs::path& dumpFile,
                                const fs::path& outputFile,
                                DWORD timeoutMs)
{
    m_error.clear();

    fs::path cdbPath = FindCdb();
    if (cdbPath.empty())
    {
        m_error = L"cdb.exe not found on system";
        return false;
    }

    // Build command: cdb -z <dump> -c ".ecxr; !analyze -v; k; lm; q"
    std::wstring cmdLine = L"\"" + cdbPath.wstring() + L"\"";
    cmdLine += L" -z \"" + dumpFile.wstring() + L"\"";
    cmdLine += L" -c \".ecxr; !analyze -v; k; lm; q\"";

    Logger::Instance().Log(LogLevel::Info,
        L"[WinDbg] Running: %s", cmdLine.c_str());

    std::wstring output;
    if (!RunCommand(cmdLine, output, timeoutMs))
    {
        m_error = L"Failed to run cdb.exe";
        return false;
    }

    // Write output to file
    std::wofstream ofs(outputFile);
    if (!ofs)
    {
        m_error = L"Failed to write output file: " + outputFile.wstring();
        return false;
    }

    ofs << L"WinDbg Automated Analysis\n";
    ofs << L"=========================\n";
    ofs << L"Dump: " << dumpFile.wstring() << L"\n";
    ofs << L"CDB:  " << cdbPath.wstring() << L"\n\n";
    ofs << output;
    ofs.close();

    Logger::Instance().Log(LogLevel::Info,
        L"[WinDbg] Analysis saved: %s", outputFile.wstring().c_str());
    return true;
}

void WinDbgAutomation::WriteNotFoundReport(const fs::path& outputFile)
{
    std::wofstream ofs(outputFile);
    if (!ofs)
        return;

    ofs << L"WinDbg / CDB Not Found\n";
    ofs << L"======================\n\n";
    ofs << L"The Windows Debugger (cdb.exe) was not found on this system.\n";
    ofs << L"Automated crash dump analysis with !analyze -v is not available.\n\n";
    ofs << L"Built-in analysis (using DbgHelp) was performed instead.\n";
    ofs << L"See the dump_analysis/ directory for those reports.\n\n";
    ofs << L"To enable full WinDbg analysis, install the Windows Debugging Tools:\n\n";
    ofs << L"  Download: https://aka.ms/windbg/download\n\n";
    ofs << L"  Or install via the Windows SDK:\n";
    ofs << L"    1. Run the Windows SDK installer\n";
    ofs << L"    2. Select 'Debugging Tools for Windows'\n";
    ofs << L"    3. After install, cdb.exe will be at:\n";
    ofs << L"       C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\cdb.exe\n\n";
    ofs << L"After installing, re-run with --analyze-dumps to get full analysis.\n";
}

} // namespace KTDiag
