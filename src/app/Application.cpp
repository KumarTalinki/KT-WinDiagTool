#include "Application.h"
#include "../util/Logger.h"
#include "../ui/MainWindow.h"

#include <objbase.h>
#include <CommCtrl.h>

#pragma comment(lib, "comctl32.lib")

namespace KTDiag
{

Application::Application(HINSTANCE hInstance)
    : m_hInstance(hInstance)
{
    // Initialize COM for WMI and Firewall COM interfaces
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Initialize common controls for modern UI elements
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);
}

Application::~Application()
{
    CoUninitialize();
}

int Application::Run(int nCmdShow)
{
    Logger::Instance().Log(LogLevel::Info, L"Starting KT-WinDiagTool GUI mode");

    MainWindow mainWindow(m_hInstance);
    if (!mainWindow.Create(nCmdShow))
    {
        Logger::Instance().Log(LogLevel::Error, L"Failed to create main window");
        MessageBoxW(nullptr, L"Failed to create main window.",
            L"KT-WinDiagTool", MB_ICONERROR);
        return 2;
    }

    return mainWindow.RunMessageLoop();
}

} // namespace KTDiag
