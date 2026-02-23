#include "TraceLevelPanel.h"
#include "resource.h"
#include "app/Config.h"

namespace KTDiag
{

static constexpr int MARGIN = 8;
static constexpr int TOP_OFFSET = 18;
static constexpr int ROW_HEIGHT = 28;
static constexpr int LABEL_WIDTH = 110;
static constexpr int COMBO_WIDTH = 120;
static constexpr int COMBO_DROP_HEIGHT = 150;

// Standard WPP trace levels
static const wchar_t* g_wppLevelNames[] = {
    L"Critical",       // maps to level 1
    L"Error",          // maps to level 2
    L"Warning",        // maps to level 3
    L"Informational",  // maps to level 4
    L"Verbose",        // maps to level 5
};
static constexpr int WPP_LEVEL_COUNT = _countof(g_wppLevelNames);

TraceLevelPanel::TraceLevelPanel() = default;

TraceLevelPanel::~TraceLevelPanel()
{
    if (m_font)
        DeleteObject(m_font);
}

bool TraceLevelPanel::Create(HWND parentHwnd, HINSTANCE hInstance, int x, int y, int width, int height)
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_font = CreateFontIndirectW(&ncm.lfMessageFont);

    m_groupBox = CreateWindowExW(0, L"BUTTON", L"Trace Levels",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, width, height,
        parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TRACE_GROUP)),
        hInstance, nullptr);
    SendMessageW(m_groupBox, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    // WPP Trace Level
    int row0Y = y + TOP_OFFSET;
    m_wppLabel = CreateWindowExW(0, L"STATIC", L"WPP Trace Level:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x + MARGIN, row0Y + 3, LABEL_WIDTH, 16,
        parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_WPP_LEVEL_LABEL)),
        hInstance, nullptr);
    SendMessageW(m_wppLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_wppCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        x + MARGIN + LABEL_WIDTH + 4, row0Y, COMBO_WIDTH, COMBO_DROP_HEIGHT,
        parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_WPP_LEVEL_COMBO)),
        hInstance, nullptr);
    SendMessageW(m_wppCombo, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    // Populate WPP levels
    for (int i = 0; i < WPP_LEVEL_COUNT; ++i)
        SendMessageW(m_wppCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_wppLevelNames[i]));
    // Default to Warning (index 2)
    SendMessageW(m_wppCombo, CB_SETCURSEL, 2, 0);

    // Product Log Level
    int row1Y = row0Y + ROW_HEIGHT;
    m_productLabel = CreateWindowExW(0, L"STATIC", L"Product Log Level:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x + MARGIN, row1Y + 3, LABEL_WIDTH, 16,
        parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRODUCT_LEVEL_LABEL)),
        hInstance, nullptr);
    SendMessageW(m_productLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_productCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        x + MARGIN + LABEL_WIDTH + 4, row1Y, COMBO_WIDTH, COMBO_DROP_HEIGHT,
        parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRODUCT_LEVEL_COMBO)),
        hInstance, nullptr);
    SendMessageW(m_productCombo, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    // Initially disabled until config is loaded
    EnableWindow(m_productCombo, FALSE);

    return true;
}

void TraceLevelPanel::UpdateFromConfig(const Config& config)
{
    // Update WPP combo default from config
    const auto& wppLevels = config.GetWppTraceLevels();
    if (!wppLevels.defaultLevel.empty())
    {
        for (int i = 0; i < WPP_LEVEL_COUNT; ++i)
        {
            // Case-insensitive prefix match (config may say "Warning", combo has "Warning")
            if (_wcsnicmp(g_wppLevelNames[i], wppLevels.defaultLevel.c_str(),
                         wppLevels.defaultLevel.size()) == 0)
            {
                SendMessageW(m_wppCombo, CB_SETCURSEL, i, 0);
                break;
            }
        }
    }

    // Populate product log levels from config
    SendMessageW(m_productCombo, CB_RESETCONTENT, 0, 0);
    const auto& productLevels = config.GetProductLogLevels();
    if (!productLevels.available.empty())
    {
        int defaultIdx = 0;
        for (int i = 0; i < static_cast<int>(productLevels.available.size()); ++i)
        {
            SendMessageW(m_productCombo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(productLevels.available[i].c_str()));
            if (_wcsicmp(productLevels.available[i].c_str(),
                         productLevels.defaultLevel.c_str()) == 0)
                defaultIdx = i;
        }
        SendMessageW(m_productCombo, CB_SETCURSEL, defaultIdx, 0);
        EnableWindow(m_productCombo, TRUE);
    }
    else
    {
        EnableWindow(m_productCombo, FALSE);
    }
}

int TraceLevelPanel::GetWppTraceLevel() const
{
    int sel = static_cast<int>(SendMessageW(m_wppCombo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) sel = 2;  // default to Warning
    return sel + 1;  // index 0 = Critical = level 1
}

std::wstring TraceLevelPanel::GetProductLogLevel() const
{
    int sel = static_cast<int>(SendMessageW(m_productCombo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return {};

    int len = static_cast<int>(SendMessageW(m_productCombo, CB_GETLBTEXTLEN, sel, 0));
    if (len <= 0) return {};

    std::wstring text(len, L'\0');
    SendMessageW(m_productCombo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text.data()));
    return text;
}

void TraceLevelPanel::Resize(int x, int y, int width, int height)
{
    if (m_groupBox)
        MoveWindow(m_groupBox, x, y, width, height, TRUE);

    int row0Y = y + TOP_OFFSET;
    if (m_wppLabel)
        MoveWindow(m_wppLabel, x + MARGIN, row0Y + 3, LABEL_WIDTH, 16, TRUE);
    if (m_wppCombo)
        MoveWindow(m_wppCombo, x + MARGIN + LABEL_WIDTH + 4, row0Y, COMBO_WIDTH, COMBO_DROP_HEIGHT, TRUE);

    int row1Y = row0Y + ROW_HEIGHT;
    if (m_productLabel)
        MoveWindow(m_productLabel, x + MARGIN, row1Y + 3, LABEL_WIDTH, 16, TRUE);
    if (m_productCombo)
        MoveWindow(m_productCombo, x + MARGIN + LABEL_WIDTH + 4, row1Y, COMBO_WIDTH, COMBO_DROP_HEIGHT, TRUE);
}

} // namespace KTDiag
