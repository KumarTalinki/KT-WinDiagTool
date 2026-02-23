#include "app/Application.h"
#include "app/CLIHandler.h"
#include "util/Logger.h"
#include "util/Privileges.h"

#include <Windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>

// Returns true if our immediate parent process is Windows Explorer.
// Used to detect double-click launch from File Explorer → switch to GUI mode.
static bool IsLaunchedFromExplorer()
{
    DWORD ownPid    = GetCurrentProcessId();
    DWORD parentPid = 0;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            if (pe.th32ProcessID == ownPid)
            {
                parentPid = pe.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    if (parentPid == 0)
        return false;

    HANDLE hParent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentPid);
    if (!hParent)
        return false;

    wchar_t imagePath[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hParent, 0, imagePath, &size);
    CloseHandle(hParent);

    if (!ok)
        return false;

    const wchar_t* fileName = wcsrchr(imagePath, L'\\');
    fileName = fileName ? fileName + 1 : imagePath;
    return _wcsicmp(fileName, L"explorer.exe") == 0;
}

// Returns true when the tool should open the Win32 GUI.
// GUI mode: -ui / --ui flag, or launched by double-clicking in Explorer.
// Everything else defaults to CLI mode.
static bool IsGUIMode(int argc, wchar_t* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg(argv[i]);
        if (arg == L"-ui" || arg == L"--ui")
            return true;
    }
    return IsLaunchedFromExplorer();
}

// Suppress the console window that CONSOLE subsystem creates automatically.
// Must be called before any GUI window is created to avoid two taskbar entries.
static void SuppressConsoleWindow()
{
    HWND hCon = GetConsoleWindow();
    if (hCon)
    {
        // Remove from taskbar and Alt+Tab before hiding
        LONG ex = GetWindowLongW(hCon, GWL_EXSTYLE);
        SetWindowLongW(hCon, GWL_EXSTYLE,
            (ex & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW);
        ShowWindow(hCon, SW_HIDE);
    }
    FreeConsole();
}

// Re-launch the current process elevated with the given extra argument.
// Used for GUI mode when the process is not running as administrator.
static bool SelfElevate(const wchar_t* extraArg)
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOASYNC;
    sei.lpVerb       = L"runas";
    sei.lpFile       = exePath;
    sei.lpParameters = extraArg;
    sei.nShow        = SW_SHOW;
    return ShellExecuteExW(&sei) != FALSE;
}

// Entry point: wWinMain with CONSOLE subsystem + wWinMainCRTStartup.
// CONSOLE subsystem ensures cmd.exe waits for the process synchronously,
// so CLI output appears before the next prompt (no prompt-before-output race).
// UAC is asInvoker: CLI callers run from an elevated shell; GUI mode
// self-elevates via ShellExecuteEx("runas") when needed.
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 2;

    KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool");

    const bool guiMode = IsGUIMode(argc, argv);

    if (guiMode)
    {
        // Suppress the console window before any GUI window is created.
        // This prevents a second taskbar entry and the console flash.
        SuppressConsoleWindow();

        // GUI mode requires admin. Re-launch elevated if needed.
        if (!KTDiag::Privileges::IsRunningAsAdmin())
        {
            if (!SelfElevate(L"-ui"))
            {
                // User cancelled UAC or elevation failed
                MessageBoxW(nullptr,
                    L"KT-WinDiagTool requires administrator privileges.",
                    L"KT-WinDiagTool", MB_ICONERROR | MB_OK);
            }
            LocalFree(argv);
            return 0;   // Current (unelevated) process exits; elevated one takes over
        }
    }
    else
    {
        // CLI mode: caller is responsible for running from an elevated shell.
        if (!KTDiag::Privileges::IsRunningAsAdmin())
        {
            KTDiag::Logger::Instance().Log(KTDiag::LogLevel::Error,
                L"KT-WinDiagTool requires administrator privileges.");
            fwprintf(stderr,
                L"Error: KT-WinDiagTool requires administrator privileges.\n"
                L"       Run this command from an elevated (Administrator) command prompt.\n");
            LocalFree(argv);
            return 2;
        }
    }

    KTDiag::Privileges::EnableDebugPrivilege();

    int result = 0;

    if (guiMode)
    {
        KTDiag::Application app(hInstance);
        result = app.Run(nCmdShow);
    }
    else
    {
        KTDiag::CLIHandler cli;
        result = cli.Run(argc, argv);
    }

    KTDiag::Logger::Instance().Log(KTDiag::LogLevel::Info,
        L"KT-WinDiagTool exiting with code: %d", result);

    LocalFree(argv);
    return result;
}
