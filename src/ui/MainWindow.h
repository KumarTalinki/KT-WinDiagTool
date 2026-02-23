#pragma once

#include <Windows.h>
#include "resource.h"
#include "CollectorPanel.h"
#include "ControlPanel.h"
#include "TraceLevelPanel.h"
#include "app/Config.h"
#include "collectors/CollectorEngine.h"
#include "controllers/WppController.h"
#include "controllers/PerfController.h"
#include "controllers/EtwController.h"
#include "controllers/PacketCaptureController.h"

#include <string>
#include <set>
#include <memory>
#include <thread>
#include <filesystem>

namespace KTDiag
{

// Custom message for controller thread -> main window
constexpr UINT WM_KTDIAG_CONTROLLER_STATUS = WM_APP + 6;

class MainWindow
{
public:
    explicit MainWindow(HINSTANCE hInstance);
    ~MainWindow();

    bool Create(int nCmdShow);
    int RunMessageLoop();

    HWND GetHwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool RegisterWindowClass();
    void CreateToolbar();
    void CreateStatusBar();
    void CreateChildPanels();
    void CreateConfigBar();
    void CreateOutputBar();
    void CreateActionButtons();
    void LayoutControls();

    // Command handlers
    void OnOpenConfig();
    void OnSetOutputDir();
    void OnRunCollection();
    void OnCancelCollection();
    void OnSelectAll(bool select);
    void OnReproMode();
    void OnAnalyzeDumps();
    void OnToggleWpp();
    void OnTogglePerf();
    void OnAbout();

    // Controller management
    void OnControllerStatus(ControllerType type, bool active);
    void StartControllerAsync(ControllerType type);
    void StopControllerAsync(ControllerType type);

    // State helpers
    void UpdateMenuState();
    void SetStatusText(const std::wstring& text);
    void SetConfigStatus(const std::wstring& text);
    bool LoadConfig(const std::wstring& path);

    // Instance data
    HINSTANCE m_hInstance;
    HWND m_hwnd = nullptr;
    HWND m_toolbar = nullptr;
    HWND m_statusBar = nullptr;
    HACCEL m_accel = nullptr;
    HFONT m_font = nullptr;

    // Child panels
    std::unique_ptr<CollectorPanel> m_collectorPanel;
    std::unique_ptr<ControlPanel> m_controlPanel;
    std::unique_ptr<TraceLevelPanel> m_traceLevelPanel;

    // Config bar controls
    HWND m_configLabel = nullptr;
    HWND m_configBrowseBtn = nullptr;

    // Output bar controls
    HWND m_outputLabel = nullptr;
    HWND m_outputEdit = nullptr;
    HWND m_outputBrowseBtn = nullptr;

    // Action buttons
    HWND m_btnCollect = nullptr;
    HWND m_btnRepro = nullptr;
    HWND m_btnAnalyze = nullptr;

    // Application state
    Config m_config;
    bool m_configLoaded = false;
    std::filesystem::path m_outputDir;
    bool m_collecting = false;

    // Controllers (persistent across session)
    std::unique_ptr<WppController> m_wppController;
    std::unique_ptr<PerfController> m_perfController;

    static constexpr wchar_t CLASS_NAME[] = L"KTDiagMainWindow";
    static constexpr int MIN_WIDTH = 800;
    static constexpr int MIN_HEIGHT = 600;
    static constexpr int LEFT_PANEL_WIDTH = 250;
};

} // namespace KTDiag
