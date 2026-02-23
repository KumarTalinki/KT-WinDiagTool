#include "MainWindow.h"
#include "ProgressDialog.h"
#include "ReproModeDialog.h"
#include "util/Logger.h"

#include <ShlObj.h>
#include <shellapi.h>
#include <CommCtrl.h>
#include <commdlg.h>

#pragma comment(lib, "comctl32.lib")

namespace KTDiag
{

// ============================================================================
// Constructor / Destructor
// ============================================================================

MainWindow::MainWindow(HINSTANCE hInstance)
    : m_hInstance(hInstance)
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_font = CreateFontIndirectW(&ncm.lfMessageFont);

    // Default output directory: Desktop\KT-WinDiag
    wchar_t desktop[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop);
    m_outputDir = std::filesystem::path(desktop) / L"KT-WinDiag";
}

MainWindow::~MainWindow()
{
    // Stop any active controllers
    if (m_wppController && m_wppController->IsActive())
        m_wppController->Stop();
    if (m_perfController && m_perfController->IsActive())
        m_perfController->Stop();

    if (m_font)
        DeleteObject(m_font);
}

// ============================================================================
// Window class and creation
// ============================================================================

bool MainWindow::RegisterWindowClass()
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MainWindow::WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = m_hInstance;
    wcex.hIcon   = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDM_MAINMENU);
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIconSm = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    return RegisterClassExW(&wcex) != 0;
}

bool MainWindow::Create(int nCmdShow)
{
    if (!RegisterWindowClass())
    {
        Logger::Instance().Log(LogLevel::Error, L"Failed to register window class");
        return false;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        CLASS_NAME,
        L"KT-WinDiagTool - Security Product Diagnostics",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 700,
        nullptr,
        nullptr,
        m_hInstance,
        this);

    if (!m_hwnd)
    {
        Logger::Instance().Log(LogLevel::Error, L"Failed to create main window");
        return false;
    }

    m_accel = LoadAcceleratorsW(m_hInstance, MAKEINTRESOURCEW(IDR_ACCEL));

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    return true;
}

int MainWindow::RunMessageLoop()
{
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (m_accel && TranslateAcceleratorW(m_hwnd, m_accel, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ============================================================================
// WndProc thunking
// ============================================================================

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* pThis = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = static_cast<MainWindow*>(cs->lpCreateParams);
        pThis->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis)
        return pThis->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateToolbar();
        CreateStatusBar();
        CreateChildPanels();
        CreateConfigBar();
        CreateOutputBar();
        CreateActionButtons();
        SetStatusText(L"Ready - Load a product config to begin");
        UpdateMenuState();
        return 0;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            // Auto-resize toolbar and status bar
            SendMessageW(m_toolbar, WM_SIZE, 0, 0);
            SendMessageW(m_statusBar, WM_SIZE, 0, 0);
            LayoutControls();
        }
        return 0;

    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = MIN_WIDTH;
        mmi->ptMinTrackSize.y = MIN_HEIGHT;
        return 0;
    }

    case WM_COMMAND:
    {
        WORD cmdId = LOWORD(wParam);
        WORD notifyCode = HIWORD(wParam);

        // Check child panels first
        if (m_collectorPanel && m_collectorPanel->OnCommand(cmdId, notifyCode))
            return 0;
        if (m_controlPanel && m_controlPanel->OnCommand(cmdId, notifyCode))
            return 0;

        switch (cmdId)
        {
        case IDM_FILE_OPEN_CONFIG:
        case IDC_CONFIG_BROWSE:
            OnOpenConfig();
            return 0;
        case IDM_FILE_SET_OUTPUT:
        case IDC_OUTPUT_BROWSE:
            OnSetOutputDir();
            return 0;
        case IDM_FILE_EXIT:
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return 0;
        case IDM_COLLECT_RUN:
        case IDC_BTN_COLLECT:
            OnRunCollection();
            return 0;
        case IDM_COLLECT_CANCEL:
            OnCancelCollection();
            return 0;
        case IDM_COLLECT_SELECT_ALL:
            OnSelectAll(true);
            return 0;
        case IDM_COLLECT_DESELECT:
            OnSelectAll(false);
            return 0;
        case IDM_TOOLS_REPRO:
        case IDC_BTN_REPRO:
            OnReproMode();
            return 0;
        case IDM_TOOLS_ANALYZE:
        case IDC_BTN_ANALYZE:
            OnAnalyzeDumps();
            return 0;
        case IDM_TOOLS_WPP_START:
            OnToggleWpp();
            return 0;
        case IDM_TOOLS_WPP_STOP:
            OnToggleWpp();
            return 0;
        case IDM_TOOLS_PERF_START:
            OnTogglePerf();
            return 0;
        case IDM_TOOLS_PERF_STOP:
            OnTogglePerf();
            return 0;
        case IDM_HELP_ABOUT:
            OnAbout();
            return 0;
        }
        break;
    }

    case WM_KTDIAG_CONTROLLER_STATUS:
        OnControllerStatus(static_cast<ControllerType>(wParam), lParam != 0);
        return 0;

    case WM_DROPFILES:
    {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        wchar_t filePath[MAX_PATH] = {};
        if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH))
        {
            std::wstring path(filePath);
            if (path.size() > 5 && _wcsicmp(path.c_str() + path.size() - 5, L".json") == 0)
                LoadConfig(path);
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_CLOSE:
        // Warn if controllers are active
        if ((m_wppController && m_wppController->IsActive()) ||
            (m_perfController && m_perfController->IsActive()))
        {
            if (MessageBoxW(m_hwnd,
                L"Trace controllers are still active. Stop them and exit?",
                L"KT-WinDiagTool", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
        }
        DestroyWindow(m_hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

// ============================================================================
// UI Creation
// ============================================================================

void MainWindow::CreateToolbar()
{
    m_toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP,
        0, 0, 0, 0,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDT_TOOLBAR)),
        m_hInstance, nullptr);

    SendMessageW(m_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    TBADDBITMAP tbab = { HINST_COMMCTRL, IDB_STD_SMALL_COLOR };
    int stdIdx = static_cast<int>(SendMessageW(m_toolbar, TB_ADDBITMAP, 0,
        reinterpret_cast<LPARAM>(&tbab)));

    TBBUTTON buttons[] = {
        { stdIdx + STD_FILEOPEN,   IDM_FILE_OPEN_CONFIG, TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0 },
        { 0, 0, 0, BTNS_SEP, {}, 0, 0 },
        { stdIdx + STD_FIND,       IDM_COLLECT_RUN,      TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0 },
        { stdIdx + STD_DELETE,     IDM_COLLECT_CANCEL,    0,               BTNS_BUTTON, {}, 0, 0 },
        { 0, 0, 0, BTNS_SEP, {}, 0, 0 },
        { stdIdx + STD_PROPERTIES, IDM_TOOLS_REPRO,      TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0 },
    };
    SendMessageW(m_toolbar, TB_ADDBUTTONS, _countof(buttons),
        reinterpret_cast<LPARAM>(buttons));
    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
}

void MainWindow::CreateStatusBar()
{
    m_statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        m_hwnd, nullptr, m_hInstance, nullptr);

    // 3 parts: status text, config name, controller info
    int parts[] = { 400, 600, -1 };
    SendMessageW(m_statusBar, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(parts));
}

void MainWindow::CreateChildPanels()
{
    // Collector panel (left side)
    m_collectorPanel = std::make_unique<CollectorPanel>();
    m_collectorPanel->Create(m_hwnd, m_hInstance, 4, 30, LEFT_PANEL_WIDTH - 8, 500);

    // Control panel (right side, below config bar and trace levels)
    m_controlPanel = std::make_unique<ControlPanel>();
    m_controlPanel->Create(m_hwnd, m_hInstance, LEFT_PANEL_WIDTH + 4, 130, 400, 140);

    // Set up controller toggle callback
    m_controlPanel->SetToggleCallback([this](ControllerType type)
    {
        switch (type)
        {
        case ControllerType::Wpp:
            OnToggleWpp();
            break;
        case ControllerType::Perf:
            OnTogglePerf();
            break;
        default:
            break;
        }
    });

    // Trace level panel (right side, below config bar)
    m_traceLevelPanel = std::make_unique<TraceLevelPanel>();
    m_traceLevelPanel->Create(m_hwnd, m_hInstance, LEFT_PANEL_WIDTH + 4, 60, 400, 68);
}

void MainWindow::CreateConfigBar()
{
    int x = LEFT_PANEL_WIDTH + 4;

    HWND lbl = CreateWindowExW(0, L"STATIC", L"Config:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, 36, 50, 18,
        m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_configLabel = CreateWindowExW(0, L"STATIC", L"(no config loaded)",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
        x + 54, 36, 300, 18,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONFIG_PATH_LABEL)),
        m_hInstance, nullptr);
    SendMessageW(m_configLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_configBrowseBtn = CreateWindowExW(0, L"BUTTON", L"Browse...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + 360, 34, 70, 22,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONFIG_BROWSE)),
        m_hInstance, nullptr);
    SendMessageW(m_configBrowseBtn, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
}

void MainWindow::CreateOutputBar()
{
    int x = LEFT_PANEL_WIDTH + 4;
    int y = 280;

    m_outputLabel = CreateWindowExW(0, L"STATIC", L"Output:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y + 3, 50, 18,
        m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_outputLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_outputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", m_outputDir.wstring().c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        x + 54, y, 300, 22,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OUTPUT_PATH_EDIT)),
        m_hInstance, nullptr);
    SendMessageW(m_outputEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_outputBrowseBtn = CreateWindowExW(0, L"BUTTON", L"Browse...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + 360, y, 70, 22,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OUTPUT_BROWSE)),
        m_hInstance, nullptr);
    SendMessageW(m_outputBrowseBtn, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
}

void MainWindow::CreateActionButtons()
{
    int x = LEFT_PANEL_WIDTH + 4;
    int y = 320;

    m_btnCollect = CreateWindowExW(0, L"BUTTON", L"Run Collection (F5)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, 140, 30,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_COLLECT)),
        m_hInstance, nullptr);
    SendMessageW(m_btnCollect, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_btnRepro = CreateWindowExW(0, L"BUTTON", L"Repro Mode (F6)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + 148, y, 130, 30,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REPRO)),
        m_hInstance, nullptr);
    SendMessageW(m_btnRepro, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_btnAnalyze = CreateWindowExW(0, L"BUTTON", L"Analyze Dumps",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + 286, y, 120, 30,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ANALYZE)),
        m_hInstance, nullptr);
    SendMessageW(m_btnAnalyze, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
}

void MainWindow::LayoutControls()
{
    RECT clientRect = {};
    GetClientRect(m_hwnd, &clientRect);
    int clientW = clientRect.right;
    int clientH = clientRect.bottom;

    // Get toolbar and status bar heights
    RECT tbRect = {};
    GetWindowRect(m_toolbar, &tbRect);
    int tbHeight = tbRect.bottom - tbRect.top;

    RECT sbRect = {};
    GetWindowRect(m_statusBar, &sbRect);
    int sbHeight = sbRect.bottom - sbRect.top;

    int contentTop = tbHeight + 2;
    int contentHeight = clientH - tbHeight - sbHeight - 4;

    // Left panel: CollectorPanel
    if (m_collectorPanel)
        m_collectorPanel->Resize(4, contentTop, LEFT_PANEL_WIDTH - 8, contentHeight);

    int rightX = LEFT_PANEL_WIDTH + 4;
    int rightW = clientW - LEFT_PANEL_WIDTH - 8;

    // Config bar
    int configY = contentTop + 4;
    if (m_configLabel)
        MoveWindow(m_configLabel, rightX + 54, configY, rightW - 134, 18, TRUE);
    if (m_configBrowseBtn)
        MoveWindow(m_configBrowseBtn, rightX + rightW - 74, configY - 2, 70, 22, TRUE);

    // Trace level panel
    int traceLevelY = configY + 24;
    if (m_traceLevelPanel)
        m_traceLevelPanel->Resize(rightX, traceLevelY, rightW, 68);

    // Control panel
    int controlY = traceLevelY + 72;
    if (m_controlPanel)
        m_controlPanel->Resize(rightX, controlY, rightW, 140);

    // Output bar
    int outputY = controlY + 148;
    if (m_outputLabel)
        MoveWindow(m_outputLabel, rightX, outputY + 3, 50, 18, TRUE);
    if (m_outputEdit)
        MoveWindow(m_outputEdit, rightX + 54, outputY, rightW - 134, 22, TRUE);
    if (m_outputBrowseBtn)
        MoveWindow(m_outputBrowseBtn, rightX + rightW - 74, outputY, 70, 22, TRUE);

    // Action buttons
    int btnY = outputY + 32;
    if (m_btnCollect)
        MoveWindow(m_btnCollect, rightX, btnY, 140, 30, TRUE);
    if (m_btnRepro)
        MoveWindow(m_btnRepro, rightX + 148, btnY, 130, 30, TRUE);
    if (m_btnAnalyze)
        MoveWindow(m_btnAnalyze, rightX + 286, btnY, 120, 30, TRUE);

    // Update status bar part widths
    int parts[] = { clientW / 2, clientW * 3 / 4, -1 };
    SendMessageW(m_statusBar, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(parts));
}

// ============================================================================
// Command handlers
// ============================================================================

void MainWindow::OnOpenConfig()
{
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"JSON Config Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Open Product Config";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        LoadConfig(fileName);
    }
}

bool MainWindow::LoadConfig(const std::wstring& path)
{
    Config newConfig;
    if (!newConfig.LoadFromFile(path))
    {
        MessageBoxW(m_hwnd, L"Failed to load config file.", L"Error", MB_ICONERROR);
        return false;
    }

    m_config = std::move(newConfig);
    m_configLoaded = true;

    // Update UI
    std::wstring label = m_config.GetProductName() + L" - " + path;
    SetWindowTextW(m_configLabel, label.c_str());
    SetConfigStatus(m_config.GetProductName());

    // Update trace level panel from config
    if (m_traceLevelPanel)
        m_traceLevelPanel->UpdateFromConfig(m_config);

    SetStatusText(L"Config loaded: " + m_config.GetProductName());
    UpdateMenuState();

    Logger::Instance().Log(LogLevel::Info, L"Loaded config: %s", path.c_str());
    return true;
}

void MainWindow::OnSetOutputDir()
{
    BROWSEINFOW bi = {};
    bi.hwndOwner = m_hwnd;
    bi.lpszTitle = L"Select Output Directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl)
    {
        wchar_t path[MAX_PATH] = {};
        if (SHGetPathFromIDListW(pidl, path))
        {
            m_outputDir = path;
            SetWindowTextW(m_outputEdit, path);
        }
        CoTaskMemFree(pidl);
    }
}

void MainWindow::OnRunCollection()
{
    if (!m_configLoaded)
    {
        MessageBoxW(m_hwnd, L"Please load a product config first.",
            L"KT-WinDiagTool", MB_ICONWARNING);
        return;
    }

    // Read output path from edit control
    wchar_t outPath[MAX_PATH] = {};
    GetWindowTextW(m_outputEdit, outPath, MAX_PATH);
    m_outputDir = outPath;

    if (m_outputDir.empty())
    {
        MessageBoxW(m_hwnd, L"Please specify an output directory.",
            L"KT-WinDiagTool", MB_ICONWARNING);
        return;
    }

    // Get selected collectors
    std::set<std::wstring> selectedIds;
    if (m_collectorPanel)
        selectedIds = m_collectorPanel->GetSelectedIds();

    if (selectedIds.empty())
    {
        MessageBoxW(m_hwnd, L"Please select at least one collector.",
            L"KT-WinDiagTool", MB_ICONWARNING);
        return;
    }

    // Get WPP level and other settings
    int wppLevel = 3;
    if (m_traceLevelPanel)
        wppLevel = m_traceLevelPanel->GetWppTraceLevel();

    // Show progress dialog (modal — runs collection internally)
    ProgressDialogParams params;
    params.config = &m_config;
    params.outputDir = m_outputDir;
    params.collectorIds = selectedIds;
    params.wppLevel = wppLevel;
    params.analyzeDumps = true;

    SetStatusText(L"Collecting diagnostics...");

    INT_PTR result = ProgressDialog::Show(m_hwnd, m_hInstance, params);

    if (result == IDOK && !params.wasCancelled)
    {
        std::wstring msg = L"Collection complete. Passed: " +
            std::to_wstring(params.successCount) +
            L"  Failed: " + std::to_wstring(params.failCount);

        if (!params.zipPath.empty())
            msg += L"\n\nZIP: " + params.zipPath;

        SetStatusText(msg);
        MessageBoxW(m_hwnd, msg.c_str(), L"Collection Complete", MB_ICONINFORMATION);
    }
    else
    {
        SetStatusText(L"Collection cancelled");
    }
}

void MainWindow::OnCancelCollection()
{
    // Cancel is handled inside the modal ProgressDialog
}

void MainWindow::OnSelectAll(bool select)
{
    if (m_collectorPanel)
        m_collectorPanel->SelectAll(select);
}

void MainWindow::OnReproMode()
{
    if (!m_configLoaded)
    {
        MessageBoxW(m_hwnd, L"Please load a product config first.",
            L"KT-WinDiagTool", MB_ICONWARNING);
        return;
    }

    // Read output path
    wchar_t outPath[MAX_PATH] = {};
    GetWindowTextW(m_outputEdit, outPath, MAX_PATH);
    m_outputDir = outPath;

    int wppLevel = 3;
    if (m_traceLevelPanel)
        wppLevel = m_traceLevelPanel->GetWppTraceLevel();

    ReproDialogParams params;
    params.config = &m_config;
    params.outputDir = m_outputDir;
    params.defaultWppLevel = wppLevel;

    SetStatusText(L"Repro mode...");

    INT_PTR result = ReproModeDialog::Show(m_hwnd, m_hInstance, params);

    if (result == IDOK && params.completed)
    {
        std::wstring msg = L"Repro collection complete. Passed: " +
            std::to_wstring(params.successCount) +
            L"  Failed: " + std::to_wstring(params.failCount);

        if (!params.zipPath.empty())
            msg += L"\n\nZIP: " + params.zipPath;

        SetStatusText(msg);
        MessageBoxW(m_hwnd, msg.c_str(), L"Repro Mode Complete", MB_ICONINFORMATION);
    }
    else
    {
        SetStatusText(L"Repro mode cancelled");
    }
}

void MainWindow::OnAnalyzeDumps()
{
    if (!m_configLoaded)
    {
        MessageBoxW(m_hwnd, L"Please load a product config first.",
            L"KT-WinDiagTool", MB_ICONWARNING);
        return;
    }

    // Run collection with only the dumps collector + analyze
    std::set<std::wstring> dumpOnly = { L"dumps" };

    wchar_t outPath[MAX_PATH] = {};
    GetWindowTextW(m_outputEdit, outPath, MAX_PATH);
    m_outputDir = outPath;

    ProgressDialogParams params;
    params.config = &m_config;
    params.outputDir = m_outputDir;
    params.collectorIds = dumpOnly;
    params.analyzeDumps = true;

    SetStatusText(L"Analyzing crash dumps...");
    ProgressDialog::Show(m_hwnd, m_hInstance, params);
    SetStatusText(L"Dump analysis complete");
}

void MainWindow::OnToggleWpp()
{
    if (!m_configLoaded)
    {
        MessageBoxW(m_hwnd, L"Please load a product config first.",
            L"KT-WinDiagTool", MB_ICONWARNING);
        return;
    }

    if (m_wppController && m_wppController->IsActive())
    {
        // Stop WPP
        m_controlPanel->SetControllerState(ControllerType::Wpp, ControllerState::Stopping);
        StopControllerAsync(ControllerType::Wpp);
    }
    else
    {
        // Start WPP
        m_controlPanel->SetControllerState(ControllerType::Wpp, ControllerState::Starting);
        StartControllerAsync(ControllerType::Wpp);
    }
    UpdateMenuState();
}

void MainWindow::OnTogglePerf()
{
    if (!m_configLoaded)
    {
        MessageBoxW(m_hwnd, L"Please load a product config first.",
            L"KT-WinDiagTool", MB_ICONWARNING);
        return;
    }

    if (m_perfController && m_perfController->IsActive())
    {
        m_controlPanel->SetControllerState(ControllerType::Perf, ControllerState::Stopping);
        StopControllerAsync(ControllerType::Perf);
    }
    else
    {
        m_controlPanel->SetControllerState(ControllerType::Perf, ControllerState::Starting);
        StartControllerAsync(ControllerType::Perf);
    }
    UpdateMenuState();
}

void MainWindow::OnAbout()
{
    DialogBoxParamW(m_hInstance, MAKEINTRESOURCEW(IDD_ABOUT), m_hwnd,
        [](HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) -> INT_PTR
        {
            UNREFERENCED_PARAMETER(lParam);
            if (msg == WM_COMMAND && LOWORD(wParam) == IDOK)
            {
                EndDialog(hdlg, IDOK);
                return TRUE;
            }
            if (msg == WM_CLOSE)
            {
                EndDialog(hdlg, IDOK);
                return TRUE;
            }
            return FALSE;
        }, 0);
}

// ============================================================================
// Controller async operations
// ============================================================================

void MainWindow::StartControllerAsync(ControllerType type)
{
    HWND hwnd = m_hwnd;

    if (type == ControllerType::Wpp)
    {
        int wppLevel = m_traceLevelPanel ? m_traceLevelPanel->GetWppTraceLevel() : 3;
        Config* config = &m_config;
        std::filesystem::path outputDir = m_outputDir;

        std::thread([this, hwnd, config, outputDir, wppLevel]()
        {
            m_wppController = std::make_unique<WppController>();
            bool ok = m_wppController->Start(*config, wppLevel, outputDir);
            PostMessage(hwnd, WM_KTDIAG_CONTROLLER_STATUS,
                static_cast<WPARAM>(ControllerType::Wpp), ok ? 1 : 0);
        }).detach();
    }
    else if (type == ControllerType::Perf)
    {
        Config* config = &m_config;
        std::filesystem::path outputDir = m_outputDir;

        std::thread([this, hwnd, config, outputDir]()
        {
            m_perfController = std::make_unique<PerfController>();
            bool ok = m_perfController->Start(*config, 1000, outputDir);
            PostMessage(hwnd, WM_KTDIAG_CONTROLLER_STATUS,
                static_cast<WPARAM>(ControllerType::Perf), ok ? 1 : 0);
        }).detach();
    }
}

void MainWindow::StopControllerAsync(ControllerType type)
{
    HWND hwnd = m_hwnd;

    if (type == ControllerType::Wpp)
    {
        std::thread([this, hwnd]()
        {
            if (m_wppController)
                m_wppController->Stop();
            PostMessage(hwnd, WM_KTDIAG_CONTROLLER_STATUS,
                static_cast<WPARAM>(ControllerType::Wpp), 0);
        }).detach();
    }
    else if (type == ControllerType::Perf)
    {
        std::thread([this, hwnd]()
        {
            if (m_perfController)
                m_perfController->Stop();
            PostMessage(hwnd, WM_KTDIAG_CONTROLLER_STATUS,
                static_cast<WPARAM>(ControllerType::Perf), 0);
        }).detach();
    }
}

void MainWindow::OnControllerStatus(ControllerType type, bool active)
{
    ControllerState state = active ? ControllerState::Active : ControllerState::Inactive;
    m_controlPanel->SetControllerState(type, state);
    UpdateMenuState();

    std::wstring statusMsg;
    if (type == ControllerType::Wpp)
        statusMsg = active ? L"WPP tracing started" : L"WPP tracing stopped";
    else if (type == ControllerType::Perf)
        statusMsg = active ? L"Perf logging started" : L"Perf logging stopped";
    SetStatusText(statusMsg);
}

// ============================================================================
// Helpers
// ============================================================================

void MainWindow::UpdateMenuState()
{
    HMENU hMenu = GetMenu(m_hwnd);
    if (!hMenu) return;

    bool wppActive = m_wppController && m_wppController->IsActive();
    bool perfActive = m_perfController && m_perfController->IsActive();

    EnableMenuItem(hMenu, IDM_TOOLS_WPP_START, wppActive ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, IDM_TOOLS_WPP_STOP, wppActive ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_TOOLS_PERF_START, perfActive ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, IDM_TOOLS_PERF_STOP, perfActive ? MF_ENABLED : MF_GRAYED);

    // Toolbar state
    SendMessageW(m_toolbar, TB_ENABLEBUTTON, IDM_COLLECT_RUN, m_configLoaded ? TRUE : FALSE);
    SendMessageW(m_toolbar, TB_ENABLEBUTTON, IDM_TOOLS_REPRO, m_configLoaded ? TRUE : FALSE);
}

void MainWindow::SetStatusText(const std::wstring& text)
{
    SendMessageW(m_statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void MainWindow::SetConfigStatus(const std::wstring& text)
{
    SendMessageW(m_statusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(text.c_str()));
}

} // namespace KTDiag
