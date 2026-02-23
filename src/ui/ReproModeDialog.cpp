#include "ReproModeDialog.h"
#include "ProgressDialog.h"

#include "collectors/EnvVarCollector.h"
#include "collectors/RegistryCollector.h"
#include "collectors/InstalledProductsCollector.h"
#include "collectors/ProductLogCollector.h"
#include "collectors/InstallLogCollector.h"
#include "collectors/ServicesCollector.h"
#include "collectors/ProxySettingsCollector.h"
#include "collectors/NetworkConfigCollector.h"
#include "collectors/FirewallConfigCollector.h"
#include "collectors/UserInfoCollector.h"
#include "collectors/DiskSpaceCollector.h"
#include "collectors/WerCollector.h"
#include "collectors/SystemInfoCollector.h"
#include "collectors/EventLogCollector.h"
#include "collectors/EtwLogCollector.h"
#include "collectors/WppLogCollector.h"
#include "collectors/PerfLogCollector.h"
#include "collectors/PacketCaptureCollector.h"
#include "collectors/CrashDumpCollector.h"
#include "output/ReportGenerator.h"
#include "output/ZipPackager.h"
#include "util/Logger.h"

#include <sstream>
#include <iomanip>

namespace KTDiag
{

template<typename T>
static void SafePostRepro(HWND hwnd, UINT msg, WPARAM wParam, T* data)
{
    if (!PostMessage(hwnd, msg, wParam, reinterpret_cast<LPARAM>(data)))
        delete data;
}

// Custom messages for repro dialog
static constexpr UINT WM_REPRO_STARTED   = WM_APP + 10;
static constexpr UINT WM_REPRO_STOPPED   = WM_APP + 11;
static constexpr UINT WM_REPRO_COLLECT_PROGRESS = WM_APP + 12;
static constexpr UINT WM_REPRO_COLLECT_STATUS   = WM_APP + 13;
static constexpr UINT WM_REPRO_COMPLETE  = WM_APP + 14;

static const wchar_t* g_wppLevelNames[] = {
    L"Critical",
    L"Error",
    L"Warning",
    L"Informational",
    L"Verbose",
};

// ============================================================================
// Static entry point
// ============================================================================

INT_PTR ReproModeDialog::Show(HWND parentHwnd, HINSTANCE hInstance, ReproDialogParams& params)
{
    return DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_REPRO), parentHwnd,
        ReproModeDialog::DlgProc, reinterpret_cast<LPARAM>(&params));
}

INT_PTR CALLBACK ReproModeDialog::DlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ReproModeDialog* pThis = nullptr;

    if (msg == WM_INITDIALOG)
    {
        auto* params = reinterpret_cast<ReproDialogParams*>(lParam);
        HINSTANCE hInst = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(hdlg, GWLP_HINSTANCE));
        pThis = new ReproModeDialog(hdlg, hInst, *params);
        SetWindowLongPtrW(hdlg, DWLP_USER, reinterpret_cast<LONG_PTR>(pThis));
        pThis->InitializeControls();
        return TRUE;
    }

    pThis = reinterpret_cast<ReproModeDialog*>(GetWindowLongPtrW(hdlg, DWLP_USER));
    if (pThis)
        return pThis->HandleMessage(msg, wParam, lParam);

    return FALSE;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

ReproModeDialog::ReproModeDialog(HWND hdlg, HINSTANCE hInstance, ReproDialogParams& params)
    : m_hdlg(hdlg)
    , m_hInstance(hInstance)
    , m_params(params)
{
    m_stepLabel = GetDlgItem(hdlg, IDC_REPRO_STEP_LABEL);
    m_wppLabel = GetDlgItem(hdlg, IDC_REPRO_WPP_LABEL);
    m_wppLevelCombo = GetDlgItem(hdlg, IDC_REPRO_WPP_LEVEL);
    m_elapsedLabel = GetDlgItem(hdlg, IDC_REPRO_ELAPSED);
    m_statusList = GetDlgItem(hdlg, IDC_REPRO_STATUS_LIST);
    m_progressBar = GetDlgItem(hdlg, IDC_REPRO_PROGRESS);
    m_collectionList = GetDlgItem(hdlg, IDC_REPRO_LISTVIEW);
    m_startBtn = GetDlgItem(hdlg, IDC_REPRO_START);
    m_stopBtn = GetDlgItem(hdlg, IDC_REPRO_STOP);
    m_closeBtn = GetDlgItem(hdlg, IDC_REPRO_CLOSE);
}

ReproModeDialog::~ReproModeDialog()
{
    KillTimer(m_hdlg, IDT_REPRO_ELAPSED);
    if (m_thread.joinable())
        m_thread.join();
}

// ============================================================================
// Initialization
// ============================================================================

void ReproModeDialog::InitializeControls()
{
    // Populate WPP level combo
    for (int i = 0; i < _countof(g_wppLevelNames); ++i)
        SendMessageW(m_wppLevelCombo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(g_wppLevelNames[i]));

    // Set default (m_params.defaultWppLevel is 1-based)
    int defaultIdx = m_params.defaultWppLevel - 1;
    if (defaultIdx < 0) defaultIdx = 2;
    if (defaultIdx >= _countof(g_wppLevelNames)) defaultIdx = 2;
    SendMessageW(m_wppLevelCombo, CB_SETCURSEL, defaultIdx, 0);

    // Set up status list columns
    ListView_SetExtendedListViewStyle(m_statusList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 120;
    col.pszText = const_cast<LPWSTR>(L"Component");
    ListView_InsertColumn(m_statusList, 0, &col);
    col.cx = 240;
    col.pszText = const_cast<LPWSTR>(L"Status");
    ListView_InsertColumn(m_statusList, 1, &col);

    // Start in Configure step
    SetStep(ReproStep::Configure);
}

void ReproModeDialog::InitializeCollectionListView()
{
    ListView_SetExtendedListViewStyle(m_collectionList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    col.cx = 160;
    col.pszText = const_cast<LPWSTR>(L"Collector");
    ListView_InsertColumn(m_collectionList, 0, &col);
    col.cx = 150;
    col.pszText = const_cast<LPWSTR>(L"Status");
    ListView_InsertColumn(m_collectionList, 1, &col);
    col.cx = 50;
    col.fmt = LVCFMT_RIGHT;
    col.pszText = const_cast<LPWSTR>(L"Time");
    ListView_InsertColumn(m_collectionList, 2, &col);
}

// ============================================================================
// Step management
// ============================================================================

void ReproModeDialog::SetStep(ReproStep step)
{
    m_currentStep = step;

    switch (step)
    {
    case ReproStep::Configure:
        SetWindowTextW(m_stepLabel, L"Step 1 of 3: Configure traces");
        ShowWindow(m_wppLabel, SW_SHOW);
        ShowWindow(m_wppLevelCombo, SW_SHOW);
        EnableWindow(m_wppLevelCombo, TRUE);
        ShowWindow(m_elapsedLabel, SW_HIDE);
        ShowWindow(m_statusList, SW_HIDE);
        ShowWindow(m_progressBar, SW_HIDE);
        ShowWindow(m_collectionList, SW_HIDE);
        EnableWindow(m_startBtn, TRUE);
        EnableWindow(m_stopBtn, FALSE);
        break;

    case ReproStep::Recording:
        SetWindowTextW(m_stepLabel, L"Step 2 of 3: Recording - Reproduce the issue now");
        ShowWindow(m_wppLabel, SW_HIDE);
        ShowWindow(m_wppLevelCombo, SW_HIDE);
        ShowWindow(m_elapsedLabel, SW_SHOW);
        SetWindowTextW(m_elapsedLabel, L"Elapsed: 00:00:00");
        ShowWindow(m_statusList, SW_SHOW);
        ShowWindow(m_progressBar, SW_HIDE);
        ShowWindow(m_collectionList, SW_HIDE);
        EnableWindow(m_startBtn, FALSE);
        EnableWindow(m_stopBtn, TRUE);
        break;

    case ReproStep::Collecting:
        SetWindowTextW(m_stepLabel, L"Step 3 of 3: Collecting diagnostics...");
        ShowWindow(m_elapsedLabel, SW_HIDE);
        ShowWindow(m_statusList, SW_HIDE);
        ShowWindow(m_progressBar, SW_SHOW);
        ShowWindow(m_collectionList, SW_SHOW);
        EnableWindow(m_startBtn, FALSE);
        EnableWindow(m_stopBtn, FALSE);

        SendMessageW(m_progressBar, PBM_SETRANGE32, 0, 100);
        SendMessageW(m_progressBar, PBM_SETPOS, 0, 0);
        InitializeCollectionListView();
        break;
    }
}

// ============================================================================
// Message handling
// ============================================================================

INT_PTR ReproModeDialog::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_REPRO_START:
            OnStartRecording();
            return TRUE;
        case IDC_REPRO_STOP:
            OnStopAndCollect();
            return TRUE;
        case IDC_REPRO_CLOSE:
        case IDCANCEL:
            OnClose();
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wParam == IDT_REPRO_ELAPSED)
        {
            OnTimer();
            return TRUE;
        }
        break;

    case WM_REPRO_STARTED:
    {
        auto* result = reinterpret_cast<ReproResult*>(lParam);
        UpdateStatusList(*result);
        delete result;
        return TRUE;
    }

    case WM_REPRO_STOPPED:
        // Transition to collection phase
        SetStep(ReproStep::Collecting);
        return TRUE;

    case WM_REPRO_COLLECT_PROGRESS:
    {
        int completed = static_cast<int>(wParam);
        int total = static_cast<int>(lParam);
        if (total > 0)
        {
            int pct = (completed * 100) / total;
            SendMessageW(m_progressBar, PBM_SETPOS, pct, 0);
        }
        return TRUE;
    }

    case WM_REPRO_COLLECT_STATUS:
    {
        auto* info = reinterpret_cast<CollectorStatusInfo*>(lParam);
        int idx = FindOrAddCollectionItem(info->collectorName);

        if (info->finished)
        {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(1) << info->elapsedSeconds << L"s";
            ListView_SetItemText(m_collectionList, idx, 1,
                const_cast<LPWSTR>(info->statusMessage.c_str()));
            std::wstring timeStr = ss.str();
            ListView_SetItemText(m_collectionList, idx, 2,
                const_cast<LPWSTR>(timeStr.c_str()));
        }
        else
        {
            ListView_SetItemText(m_collectionList, idx, 1,
                const_cast<LPWSTR>(info->statusMessage.c_str()));
            ListView_SetItemText(m_collectionList, idx, 2,
                const_cast<LPWSTR>(L"..."));
        }
        ListView_EnsureVisible(m_collectionList, idx, FALSE);

        delete info;
        return TRUE;
    }

    case WM_REPRO_COMPLETE:
    {
        m_finished = true;
        m_params.completed = true;
        m_params.successCount = static_cast<int>(wParam);
        m_params.failCount = static_cast<int>(lParam);

        SendMessageW(m_progressBar, PBM_SETPOS, 100, 0);

        std::wstring statusMsg = L"Step 3 of 3: Complete. Passed: " +
            std::to_wstring(m_params.successCount) +
            L"  Failed: " + std::to_wstring(m_params.failCount);
        SetWindowTextW(m_stepLabel, statusMsg.c_str());

        EnableWindow(m_closeBtn, TRUE);
        SetFocus(m_closeBtn);
        return TRUE;
    }

    case WM_CLOSE:
        OnClose();
        return TRUE;
    }

    return FALSE;
}

// ============================================================================
// Actions
// ============================================================================

void ReproModeDialog::OnStartRecording()
{
    // Get WPP level from combo
    int sel = static_cast<int>(SendMessageW(m_wppLevelCombo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) sel = 2;
    int wppLevel = sel + 1;

    SetStep(ReproStep::Recording);
    m_recordingStart = std::chrono::steady_clock::now();
    SetTimer(m_hdlg, IDT_REPRO_ELAPSED, 1000, nullptr);

    HWND hdlg = m_hdlg;
    const Config* config = m_params.config;
    std::filesystem::path outputDir = m_params.outputDir;

    m_orchestrator = std::make_unique<ReproOrchestrator>();

    m_thread = std::thread([this, hdlg, config, outputDir, wppLevel]()
    {
        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);

        ReproResult result = m_orchestrator->StartRepro(*config, outputDir, wppLevel);

        auto* resultCopy = new ReproResult(result);
        SafePostRepro(hdlg, WM_REPRO_STARTED, 0, resultCopy);
    });
}

void ReproModeDialog::OnStopAndCollect()
{
    if (m_stopping.exchange(true))
        return;

    KillTimer(m_hdlg, IDT_REPRO_ELAPSED);
    EnableWindow(m_stopBtn, FALSE);
    EnableWindow(m_closeBtn, FALSE);
    SetWindowTextW(m_stepLabel, L"Stopping traces and collecting...");

    // Wait for start thread to finish
    if (m_thread.joinable())
        m_thread.join();

    HWND hdlg = m_hdlg;
    const Config* config = m_params.config;
    std::filesystem::path outputDir = m_params.outputDir;

    m_thread = std::thread([this, hdlg, config, outputDir]()
    {
        // Stop repro
        m_orchestrator->StopRepro();
        PostMessage(hdlg, WM_REPRO_STOPPED, 0, 0);

        // Run full collection
        m_engine = std::make_unique<CollectorEngine>();
        RegisterAllCollectors(*m_engine);

        EngineProgressCallback engineCb = [hdlg](int completed, int total, const std::wstring& name)
        {
            UNREFERENCED_PARAMETER(name);
            PostMessage(hdlg, WM_REPRO_COLLECT_PROGRESS, completed, total);
        };

        ProgressCallback collectorCb = [hdlg](const std::wstring& name,
            const std::wstring& status, int pct)
        {
            auto* info = new CollectorStatusInfo();
            info->collectorName = name;
            info->statusMessage = status;
            info->percentComplete = pct;
            info->finished = false;
            SafePostRepro(hdlg, WM_REPRO_COLLECT_STATUS, 0, info);
        };

        std::set<std::wstring> emptyFilter;  // collect all
        m_engine->RunAll(*config, outputDir, emptyFilter, engineCb, collectorCb);

        // Post final results
        for (const auto& result : m_engine->GetResults())
        {
            auto* info = new CollectorStatusInfo();
            info->collectorName = result.collectorName;
            info->finished = true;
            info->success = result.success;
            info->elapsedSeconds = result.elapsedSeconds;
            info->errorMessage = result.errorMessage;
            info->statusMessage = result.success ? L"OK" : (L"FAILED: " + result.errorMessage);
            SafePostRepro(hdlg, WM_REPRO_COLLECT_STATUS, 0, info);
        }

        // Generate report and ZIP
        ReportGenerator reportGen;
        reportGen.Generate(outputDir, *config, m_engine->GetResults());

        ZipPackager packager;
        if (packager.Package(outputDir, config->GetProductName()))
        {
            m_params.zipPath = packager.GetZipPath();
        }

        PostMessage(hdlg, WM_REPRO_COMPLETE,
            m_engine->SuccessCount(), m_engine->FailureCount());
    });
}

void ReproModeDialog::OnTimer()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_recordingStart);

    int totalSec = static_cast<int>(elapsed.count());
    int hours = totalSec / 3600;
    int minutes = (totalSec % 3600) / 60;
    int seconds = totalSec % 60;

    wchar_t buf[64];
    swprintf_s(buf, L"Elapsed: %02d:%02d:%02d  -  Reproduce the issue, then click Stop && Collect",
        hours, minutes, seconds);
    SetWindowTextW(m_elapsedLabel, buf);
}

void ReproModeDialog::OnClose()
{
    // If repro is still active, stop it first
    if (m_orchestrator && m_orchestrator->IsActive())
    {
        if (MessageBoxW(m_hdlg,
            L"Traces are still recording. Stop them and close?",
            L"Repro Mode", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;

        KillTimer(m_hdlg, IDT_REPRO_ELAPSED);
        m_orchestrator->StopRepro();
    }

    if (m_thread.joinable())
        m_thread.join();

    SetWindowLongPtrW(m_hdlg, DWLP_USER, 0);
    delete this;
    EndDialog(m_hdlg, m_finished ? IDOK : IDCANCEL);
}

// ============================================================================
// Helpers
// ============================================================================

void ReproModeDialog::UpdateStatusList(const ReproResult& result)
{
    ListView_DeleteAllItems(m_statusList);

    auto addRow = [this](const wchar_t* name, bool started, const wchar_t* errText)
    {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = ListView_GetItemCount(m_statusList);
        item.pszText = const_cast<LPWSTR>(name);
        int idx = ListView_InsertItem(m_statusList, &item);

        const wchar_t* status = started ? L"Started" : (errText[0] ? errText : L"Not started");
        ListView_SetItemText(m_statusList, idx, 1, const_cast<LPWSTR>(status));
    };

    addRow(L"WPP Tracing", result.wppStarted, L"");
    addRow(L"ETW Tracing", result.etwStarted, L"");
    addRow(L"Perf Logging", result.perfStarted, L"");
    addRow(L"Packet Capture", result.packetStarted, L"");

    // Show errors if any
    for (const auto& err : result.errors)
    {
        Logger::Instance().Log(LogLevel::Warning, L"Repro start error: %s", err.c_str());
    }
}

int ReproModeDialog::FindOrAddCollectionItem(const std::wstring& name)
{
    auto it = m_collectionItemMap.find(name);
    if (it != m_collectionItemMap.end())
        return it->second;

    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = m_nextCollectionItem;
    item.pszText = const_cast<LPWSTR>(name.c_str());
    ListView_InsertItem(m_collectionList, &item);
    ListView_SetItemText(m_collectionList, m_nextCollectionItem, 1,
        const_cast<LPWSTR>(L"Waiting..."));

    m_collectionItemMap[name] = m_nextCollectionItem;
    return m_nextCollectionItem++;
}

} // namespace KTDiag
