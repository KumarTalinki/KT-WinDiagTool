#pragma once

#include <Windows.h>
#include <CommCtrl.h>
#include "resource.h"
#include "app/Config.h"
#include "controllers/ReproOrchestrator.h"
#include "collectors/CollectorEngine.h"

#include <string>
#include <filesystem>
#include <thread>
#include <memory>
#include <atomic>
#include <chrono>
#include <set>
#include <map>

namespace KTDiag
{

struct ReproDialogParams
{
    const Config* config = nullptr;
    std::filesystem::path outputDir;
    int defaultWppLevel = 3;

    // Output
    bool completed = false;
    std::wstring zipPath;
    int successCount = 0;
    int failCount = 0;
};

enum class ReproStep { Configure, Recording, Collecting };

class ReproModeDialog
{
public:
    static INT_PTR Show(HWND parentHwnd, HINSTANCE hInstance, ReproDialogParams& params);

private:
    static INT_PTR CALLBACK DlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam);

    ReproModeDialog(HWND hdlg, HINSTANCE hInstance, ReproDialogParams& params);
    ~ReproModeDialog();

    INT_PTR HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void InitializeControls();
    void SetStep(ReproStep step);
    void OnStartRecording();
    void OnStopAndCollect();
    void OnTimer();
    void OnClose();

    void UpdateStatusList(const ReproResult& result);
    void InitializeCollectionListView();
    int FindOrAddCollectionItem(const std::wstring& name);

    HWND m_hdlg;
    HINSTANCE m_hInstance;
    ReproDialogParams& m_params;
    ReproStep m_currentStep = ReproStep::Configure;

    // Controls
    HWND m_stepLabel = nullptr;
    HWND m_wppLevelCombo = nullptr;
    HWND m_wppLabel = nullptr;
    HWND m_elapsedLabel = nullptr;
    HWND m_statusList = nullptr;
    HWND m_progressBar = nullptr;
    HWND m_collectionList = nullptr;
    HWND m_startBtn = nullptr;
    HWND m_stopBtn = nullptr;
    HWND m_closeBtn = nullptr;

    // Repro state
    std::unique_ptr<ReproOrchestrator> m_orchestrator;
    std::unique_ptr<CollectorEngine> m_engine;
    std::thread m_thread;
    std::atomic<bool> m_stopping{ false };
    std::chrono::steady_clock::time_point m_recordingStart;
    bool m_finished = false;

    // Collection ListView tracking
    std::map<std::wstring, int> m_collectionItemMap;
    int m_nextCollectionItem = 0;
};

} // namespace KTDiag
