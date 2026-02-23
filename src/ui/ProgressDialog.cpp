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
#include "analysis/DumpAnalyzer.h"
#include "analysis/WinDbgAutomation.h"
#include "output/ReportGenerator.h"
#include "output/ZipPackager.h"
#include "util/Logger.h"

#include <sstream>
#include <iomanip>

namespace KTDiag
{

// Helper: safely post heap-allocated data via PostMessage
template<typename T>
static void SafePostMessage(HWND hwnd, UINT msg, WPARAM wParam, T* data)
{
    if (!PostMessage(hwnd, msg, wParam, reinterpret_cast<LPARAM>(data)))
        delete data;
}

void RegisterAllCollectors(CollectorEngine& engine)
{
    engine.AddCollector(std::make_unique<EnvVarCollector>());
    engine.AddCollector(std::make_unique<RegistryCollector>());
    engine.AddCollector(std::make_unique<InstalledProductsCollector>());
    engine.AddCollector(std::make_unique<ProductLogCollector>());
    engine.AddCollector(std::make_unique<InstallLogCollector>());
    engine.AddCollector(std::make_unique<ServicesCollector>());
    engine.AddCollector(std::make_unique<ProxySettingsCollector>());
    engine.AddCollector(std::make_unique<NetworkConfigCollector>());
    engine.AddCollector(std::make_unique<FirewallConfigCollector>());
    engine.AddCollector(std::make_unique<UserInfoCollector>());
    engine.AddCollector(std::make_unique<DiskSpaceCollector>());
    engine.AddCollector(std::make_unique<WerCollector>());
    engine.AddCollector(std::make_unique<SystemInfoCollector>());
    engine.AddCollector(std::make_unique<EventLogCollector>());
    engine.AddCollector(std::make_unique<EtwLogCollector>());
    engine.AddCollector(std::make_unique<WppLogCollector>());
    engine.AddCollector(std::make_unique<PerfLogCollector>());
    engine.AddCollector(std::make_unique<PacketCaptureCollector>());
    engine.AddCollector(std::make_unique<CrashDumpCollector>());
}

// ============================================================================
// Static entry point
// ============================================================================

INT_PTR ProgressDialog::Show(HWND parentHwnd, HINSTANCE hInstance, ProgressDialogParams& params)
{
    return DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_PROGRESS), parentHwnd,
        ProgressDialog::DlgProc, reinterpret_cast<LPARAM>(&params));
}

INT_PTR CALLBACK ProgressDialog::DlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ProgressDialog* pThis = nullptr;

    if (msg == WM_INITDIALOG)
    {
        auto* params = reinterpret_cast<ProgressDialogParams*>(lParam);
        HINSTANCE hInst = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(hdlg, GWLP_HINSTANCE));
        pThis = new ProgressDialog(hdlg, hInst, *params);
        SetWindowLongPtrW(hdlg, DWLP_USER, reinterpret_cast<LONG_PTR>(pThis));
        pThis->InitializeListView();
        pThis->StartCollectionThread();
        return TRUE;
    }

    pThis = reinterpret_cast<ProgressDialog*>(GetWindowLongPtrW(hdlg, DWLP_USER));
    if (pThis)
    {
        INT_PTR result = pThis->HandleMessage(msg, wParam, lParam);

        if (msg == WM_DESTROY || (msg == WM_COMMAND &&
            (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)))
        {
            // Clean up on dialog close
            if (msg == WM_DESTROY)
            {
                SetWindowLongPtrW(hdlg, DWLP_USER, 0);
                delete pThis;
            }
        }

        return result;
    }

    return FALSE;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

ProgressDialog::ProgressDialog(HWND hdlg, HINSTANCE hInstance, ProgressDialogParams& params)
    : m_hdlg(hdlg)
    , m_hInstance(hInstance)
    , m_params(params)
{
    m_progressBar = GetDlgItem(hdlg, IDC_PROG_OVERALL);
    m_statusText = GetDlgItem(hdlg, IDC_PROG_STATUS_TEXT);
    m_listView = GetDlgItem(hdlg, IDC_PROG_LISTVIEW);
    m_cancelBtn = GetDlgItem(hdlg, IDC_PROG_CANCEL);
}

ProgressDialog::~ProgressDialog()
{
    if (m_thread.joinable())
        m_thread.join();
}

// ============================================================================
// Message handling
// ============================================================================

INT_PTR ProgressDialog::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PROG_CANCEL || LOWORD(wParam) == IDCANCEL)
        {
            if (m_finished)
            {
                if (m_thread.joinable())
                    m_thread.join();
                SetWindowLongPtrW(m_hdlg, DWLP_USER, 0);
                delete this;
                EndDialog(m_hdlg, m_params.wasCancelled ? IDCANCEL : IDOK);
                return TRUE;
            }
            OnCancel();
            return TRUE;
        }
        break;

    case WM_KTDIAG_PROGRESS_UPDATE:
        OnProgressUpdate(static_cast<int>(wParam), static_cast<int>(lParam));
        return TRUE;

    case WM_KTDIAG_COLLECTOR_STATUS:
    {
        auto* info = reinterpret_cast<CollectorStatusInfo*>(lParam);
        OnCollectorStatus(info);
        delete info;
        return TRUE;
    }

    case WM_KTDIAG_COLLECTION_COMPLETE:
        OnCollectionComplete(static_cast<int>(wParam), static_cast<int>(lParam));
        return TRUE;

    case WM_KTDIAG_ZIP_COMPLETE:
    {
        auto* path = reinterpret_cast<wchar_t*>(lParam);
        OnZipComplete(path);
        delete[] path;
        return TRUE;
    }

    case WM_CLOSE:
        if (m_finished)
        {
            if (m_thread.joinable())
                m_thread.join();
            SetWindowLongPtrW(m_hdlg, DWLP_USER, 0);
            delete this;
            EndDialog(m_hdlg, m_params.wasCancelled ? IDCANCEL : IDOK);
            return TRUE;
        }
        OnCancel();
        return TRUE;
    }

    return FALSE;
}

// ============================================================================
// ListView setup
// ============================================================================

void ProgressDialog::InitializeListView()
{
    // Set extended styles
    ListView_SetExtendedListViewStyle(m_listView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // Add columns: Name, Status, Time
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

    col.fmt = LVCFMT_LEFT;
    col.cx = 180;
    col.pszText = const_cast<LPWSTR>(L"Collector");
    ListView_InsertColumn(m_listView, 0, &col);

    col.cx = 160;
    col.pszText = const_cast<LPWSTR>(L"Status");
    ListView_InsertColumn(m_listView, 1, &col);

    col.cx = 50;
    col.fmt = LVCFMT_RIGHT;
    col.pszText = const_cast<LPWSTR>(L"Time");
    ListView_InsertColumn(m_listView, 2, &col);
}

int ProgressDialog::FindOrAddListViewItem(const std::wstring& name)
{
    auto it = m_itemMap.find(name);
    if (it != m_itemMap.end())
        return it->second;

    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = m_nextItemIndex;
    item.iSubItem = 0;
    item.pszText = const_cast<LPWSTR>(name.c_str());
    ListView_InsertItem(m_listView, &item);

    // Set initial status
    ListView_SetItemText(m_listView, m_nextItemIndex, 1, const_cast<LPWSTR>(L"Waiting..."));
    ListView_SetItemText(m_listView, m_nextItemIndex, 2, const_cast<LPWSTR>(L""));

    m_itemMap[name] = m_nextItemIndex;
    return m_nextItemIndex++;
}

void ProgressDialog::SetListViewItemStatus(int index, const std::wstring& status, const std::wstring& time)
{
    ListView_SetItemText(m_listView, index, 1, const_cast<LPWSTR>(status.c_str()));
    ListView_SetItemText(m_listView, index, 2, const_cast<LPWSTR>(time.c_str()));
}

// ============================================================================
// Collection thread
// ============================================================================

void ProgressDialog::StartCollectionThread()
{
    // Set progress bar range
    SendMessageW(m_progressBar, PBM_SETRANGE32, 0, 100);
    SendMessageW(m_progressBar, PBM_SETPOS, 0, 0);

    m_engine = std::make_unique<CollectorEngine>();
    RegisterAllCollectors(*m_engine);

    HWND hdlg = m_hdlg;
    const Config* config = m_params.config;
    std::filesystem::path outputDir = m_params.outputDir;
    std::set<std::wstring> filterIds = m_params.collectorIds;
    bool analyzeDumps = m_params.analyzeDumps;

    m_thread = std::thread([this, hdlg, config, outputDir, filterIds, analyzeDumps]()
    {
        // Create output directory
        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);

        // Engine progress callback
        EngineProgressCallback engineCb = [hdlg](int completed, int total, const std::wstring& name)
        {
            UNREFERENCED_PARAMETER(name);
            PostMessage(hdlg, WM_KTDIAG_PROGRESS_UPDATE, completed, total);
        };

        // Per-collector progress callback
        ProgressCallback collectorCb = [hdlg](const std::wstring& name, const std::wstring& status, int pct)
        {
            auto* info = new CollectorStatusInfo();
            info->collectorName = name;
            info->statusMessage = status;
            info->percentComplete = pct;
            info->finished = false;
            SafePostMessage(hdlg, WM_KTDIAG_COLLECTOR_STATUS, 0, info);
        };

        // Run collectors
        bool allOk = m_engine->RunAll(*config, outputDir, filterIds, engineCb, collectorCb);
        UNREFERENCED_PARAMETER(allOk);

        // Post individual results for completed collectors
        for (const auto& result : m_engine->GetResults())
        {
            auto* info = new CollectorStatusInfo();
            info->collectorName = result.collectorName;
            info->finished = true;
            info->success = result.success;
            info->elapsedSeconds = result.elapsedSeconds;
            info->errorMessage = result.errorMessage;
            info->statusMessage = result.success ? L"OK" : (L"FAILED: " + result.errorMessage);
            SafePostMessage(hdlg, WM_KTDIAG_COLLECTOR_STATUS, 0, info);
        }

        // Crash dump analysis (optional)
        if (analyzeDumps)
        {
            std::filesystem::path dumpsDir = outputDir / L"dumps";
            if (std::filesystem::exists(dumpsDir))
            {
                std::vector<std::filesystem::path> dumpFiles;
                for (const auto& entry : std::filesystem::directory_iterator(dumpsDir, ec))
                {
                    if (entry.path().extension() == L".dmp")
                        dumpFiles.push_back(entry.path());
                }

                if (!dumpFiles.empty())
                {
                    std::filesystem::path analysisDir = outputDir / L"dump_analysis";
                    std::filesystem::create_directories(analysisDir, ec);

                    DumpAnalyzer analyzer;
                    for (const auto& dmpFile : dumpFiles)
                    {
                        auto result = analyzer.Analyze(dmpFile);
                        std::filesystem::path reportFile = analysisDir /
                            (dmpFile.stem().wstring() + L"_analysis.txt");
                        DumpAnalyzer::WriteReport(result, reportFile);
                    }

                    // WinDbg automation
                    std::filesystem::path cdbPath = WinDbgAutomation::FindCdb();
                    if (!cdbPath.empty())
                    {
                        std::filesystem::path windbgDir = outputDir / L"windbg_analysis";
                        std::filesystem::create_directories(windbgDir, ec);

                        WinDbgAutomation windbg;
                        for (const auto& dmpFile : dumpFiles)
                        {
                            std::filesystem::path outFile = windbgDir /
                                (dmpFile.stem().wstring() + L"_windbg.txt");
                            windbg.Analyze(dmpFile, outFile);
                        }
                    }
                    else
                    {
                        WinDbgAutomation::WriteNotFoundReport(outputDir / L"windbg_not_found.txt");
                    }
                }
            }
        }

        // Generate report
        ReportGenerator reportGen;
        reportGen.Generate(outputDir, *config, m_engine->GetResults());

        // Package ZIP
        ZipPackager packager;
        if (packager.Package(outputDir, config->GetProductName()))
        {
            std::wstring zipPath = packager.GetZipPath();
            size_t len = zipPath.size() + 1;
            wchar_t* pathCopy = new wchar_t[len];
            wcscpy_s(pathCopy, len, zipPath.c_str());
            SafePostMessage(hdlg, WM_KTDIAG_ZIP_COMPLETE, 0, pathCopy);
        }

        PostMessage(hdlg, WM_KTDIAG_COLLECTION_COMPLETE,
            m_engine->SuccessCount(), m_engine->FailureCount());
    });
}

void ProgressDialog::OnCancel()
{
    if (m_cancelled.exchange(true))
        return;  // Already cancelling

    m_params.wasCancelled = true;
    SetWindowTextW(m_statusText, L"Cancelling...");
    EnableWindow(m_cancelBtn, FALSE);

    if (m_engine)
        m_engine->Cancel();
}

// ============================================================================
// Message handlers
// ============================================================================

void ProgressDialog::OnProgressUpdate(int completed, int total)
{
    if (total > 0)
    {
        int pct = (completed * 100) / total;
        SendMessageW(m_progressBar, PBM_SETPOS, pct, 0);

        std::wstring statusMsg = L"Collecting... (" + std::to_wstring(completed) +
            L"/" + std::to_wstring(total) + L")";
        SetWindowTextW(m_statusText, statusMsg.c_str());
    }
}

void ProgressDialog::OnCollectorStatus(CollectorStatusInfo* info)
{
    int idx = FindOrAddListViewItem(info->collectorName);

    if (info->finished)
    {
        // Format elapsed time
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(1) << info->elapsedSeconds << L"s";

        SetListViewItemStatus(idx, info->statusMessage, ss.str());

        // Ensure the latest item is visible
        ListView_EnsureVisible(m_listView, idx, FALSE);
    }
    else
    {
        // In-progress update
        std::wstring status = info->statusMessage;
        if (info->percentComplete > 0)
            status += L" (" + std::to_wstring(info->percentComplete) + L"%)";
        SetListViewItemStatus(idx, status, L"...");
    }
}

void ProgressDialog::OnCollectionComplete(int successCount, int failCount)
{
    m_finished = true;
    m_params.successCount = successCount;
    m_params.failCount = failCount;

    SendMessageW(m_progressBar, PBM_SETPOS, 100, 0);

    std::wstring statusMsg;
    if (m_params.wasCancelled)
    {
        statusMsg = L"Collection cancelled. Passed: " + std::to_wstring(successCount) +
            L"  Failed: " + std::to_wstring(failCount);
    }
    else
    {
        statusMsg = L"Collection complete. Passed: " + std::to_wstring(successCount) +
            L"  Failed: " + std::to_wstring(failCount);
    }
    SetWindowTextW(m_statusText, statusMsg.c_str());

    // Change Cancel to Close
    SetWindowTextW(m_cancelBtn, L"Close");
    EnableWindow(m_cancelBtn, TRUE);
}

void ProgressDialog::OnZipComplete(wchar_t* zipPath)
{
    m_params.zipPath = zipPath;
}

} // namespace KTDiag
