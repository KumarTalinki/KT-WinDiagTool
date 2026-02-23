#pragma once

#include <Windows.h>
#include <CommCtrl.h>
#include "resource.h"
#include "app/Config.h"
#include "collectors/CollectorEngine.h"

#include <string>
#include <set>
#include <filesystem>
#include <thread>
#include <memory>
#include <vector>
#include <atomic>
#include <map>

namespace KTDiag
{

// Custom messages for collection thread -> dialog communication
constexpr UINT WM_KTDIAG_PROGRESS_UPDATE     = WM_APP + 1;
constexpr UINT WM_KTDIAG_COLLECTOR_STATUS    = WM_APP + 2;
constexpr UINT WM_KTDIAG_COLLECTION_COMPLETE = WM_APP + 3;
constexpr UINT WM_KTDIAG_ZIP_COMPLETE        = WM_APP + 7;

struct CollectorStatusInfo
{
    std::wstring collectorName;
    std::wstring statusMessage;
    int percentComplete = 0;
    bool finished = false;
    bool success = false;
    double elapsedSeconds = 0.0;
    std::wstring errorMessage;
};

struct ProgressDialogParams
{
    const Config* config = nullptr;
    std::filesystem::path outputDir;
    std::set<std::wstring> collectorIds;
    int wppLevel = 3;
    bool analyzeDumps = false;

    // Output — filled by dialog
    int successCount = 0;
    int failCount = 0;
    std::wstring zipPath;
    bool wasCancelled = false;
};

// Shared helper: registers all 19 collectors into an engine
void RegisterAllCollectors(CollectorEngine& engine);

class ProgressDialog
{
public:
    static INT_PTR Show(HWND parentHwnd, HINSTANCE hInstance, ProgressDialogParams& params);

private:
    static INT_PTR CALLBACK DlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam);

    ProgressDialog(HWND hdlg, HINSTANCE hInstance, ProgressDialogParams& params);
    ~ProgressDialog();

    INT_PTR HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void InitializeListView();
    void StartCollectionThread();
    void OnCancel();

    void OnProgressUpdate(int completed, int total);
    void OnCollectorStatus(CollectorStatusInfo* info);
    void OnCollectionComplete(int successCount, int failCount);
    void OnZipComplete(wchar_t* zipPath);

    int FindOrAddListViewItem(const std::wstring& name);
    void SetListViewItemStatus(int index, const std::wstring& status, const std::wstring& time);

    HWND m_hdlg;
    HINSTANCE m_hInstance;
    HWND m_progressBar = nullptr;
    HWND m_statusText = nullptr;
    HWND m_listView = nullptr;
    HWND m_cancelBtn = nullptr;
    ProgressDialogParams& m_params;
    std::thread m_thread;
    std::unique_ptr<CollectorEngine> m_engine;
    std::atomic<bool> m_cancelled{ false };
    bool m_finished = false;

    // Map collector name -> ListView index
    std::map<std::wstring, int> m_itemMap;
    int m_nextItemIndex = 0;
};

} // namespace KTDiag
