#include "CollectorPanel.h"
#include "resource.h"

namespace KTDiag
{

struct CollectorDef
{
    const wchar_t* id;
    const wchar_t* name;
};

static const CollectorDef g_collectorDefs[] = {
    { L"env",          L"Environment Variables" },
    { L"registry",     L"Registry Keys" },
    { L"installed",    L"Installed Products" },
    { L"productlogs",  L"Product Logs" },
    { L"installlogs",  L"Install Logs" },
    { L"services",     L"Services" },
    { L"proxy",        L"Proxy Settings" },
    { L"network",      L"Network Configuration" },
    { L"firewall",     L"Firewall Configuration" },
    { L"userinfo",     L"User Information" },
    { L"disk",         L"Disk Space" },
    { L"wer",          L"Windows Error Reports" },
    { L"sysinfo",      L"System Information" },
    { L"eventlog",     L"Event Logs" },
    { L"etw",          L"ETW Logs" },
    { L"wpp",          L"WPP Trace Logs" },
    { L"perf",         L"Performance Logs" },
    { L"packets",      L"Packet Capture" },
    { L"dumps",        L"Crash Dumps" },
};

static constexpr int COLLECTOR_COUNT = _countof(g_collectorDefs);
static constexpr int CHECKBOX_HEIGHT = 20;
static constexpr int CHECKBOX_SPACING = 2;
static constexpr int BUTTON_HEIGHT = 24;
static constexpr int BUTTON_WIDTH = 80;
static constexpr int MARGIN = 8;
static constexpr int TOP_OFFSET = 18;  // below group box title

CollectorPanel::CollectorPanel() = default;

CollectorPanel::~CollectorPanel()
{
    if (m_font)
        DeleteObject(m_font);
}

bool CollectorPanel::Create(HWND parentHwnd, HINSTANCE hInstance, int x, int y, int width, int height)
{
    m_parent = parentHwnd;
    m_hInstance = hInstance;

    // Create default UI font
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_font = CreateFontIndirectW(&ncm.lfMessageFont);

    // Group box
    m_groupBox = CreateWindowExW(0, L"BUTTON", L"Collectors",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, width, height,
        parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COLLECTOR_GROUP)),
        hInstance, nullptr);
    SendMessageW(m_groupBox, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    // Select All / Deselect All buttons
    int btnY = y + TOP_OFFSET;
    m_btnSelectAll = CreateWindowExW(0, L"BUTTON", L"Select All",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + MARGIN, btnY, BUTTON_WIDTH, BUTTON_HEIGHT,
        parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SELECT_ALL)),
        hInstance, nullptr);
    SendMessageW(m_btnSelectAll, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_btnDeselectAll = CreateWindowExW(0, L"BUTTON", L"Deselect All",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + MARGIN + BUTTON_WIDTH + 4, btnY, BUTTON_WIDTH + 10, BUTTON_HEIGHT,
        parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_DESELECT_ALL)),
        hInstance, nullptr);
    SendMessageW(m_btnDeselectAll, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    InitializeCollectors();
    return true;
}

void CollectorPanel::InitializeCollectors()
{
    m_collectors.clear();

    RECT groupRect = {};
    GetWindowRect(m_groupBox, &groupRect);
    MapWindowPoints(HWND_DESKTOP, m_parent, reinterpret_cast<POINT*>(&groupRect), 2);

    int startY = groupRect.top + TOP_OFFSET + BUTTON_HEIGHT + 6;

    for (int i = 0; i < COLLECTOR_COUNT; ++i)
    {
        CollectorInfo info;
        info.id = g_collectorDefs[i].id;
        info.name = g_collectorDefs[i].name;

        int controlId = IDC_COLLECTOR_FIRST + i;
        int cbY = startY + i * (CHECKBOX_HEIGHT + CHECKBOX_SPACING);

        info.checkbox = CreateWindowExW(0, L"BUTTON", info.name.c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            groupRect.left + MARGIN, cbY,
            (groupRect.right - groupRect.left) - MARGIN * 2, CHECKBOX_HEIGHT,
            m_parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
            m_hInstance, nullptr);
        SendMessageW(info.checkbox, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

        // Check by default
        SendMessageW(info.checkbox, BM_SETCHECK, BST_CHECKED, 0);

        m_collectors.push_back(std::move(info));
    }
}

std::set<std::wstring> CollectorPanel::GetSelectedIds() const
{
    std::set<std::wstring> selected;
    for (const auto& col : m_collectors)
    {
        if (SendMessageW(col.checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED)
            selected.insert(col.id);
    }
    return selected;
}

void CollectorPanel::SelectAll(bool select)
{
    WPARAM state = select ? BST_CHECKED : BST_UNCHECKED;
    for (const auto& col : m_collectors)
    {
        SendMessageW(col.checkbox, BM_SETCHECK, state, 0);
    }
}

bool CollectorPanel::OnCommand(WORD controlId, WORD notifyCode)
{
    UNREFERENCED_PARAMETER(notifyCode);

    if (controlId == IDC_BTN_SELECT_ALL)
    {
        SelectAll(true);
        return true;
    }
    if (controlId == IDC_BTN_DESELECT_ALL)
    {
        SelectAll(false);
        return true;
    }
    return false;
}

void CollectorPanel::Resize(int x, int y, int width, int height)
{
    if (m_groupBox)
        MoveWindow(m_groupBox, x, y, width, height, TRUE);

    // Reposition buttons
    int btnY = y + TOP_OFFSET;
    if (m_btnSelectAll)
        MoveWindow(m_btnSelectAll, x + MARGIN, btnY, BUTTON_WIDTH, BUTTON_HEIGHT, TRUE);
    if (m_btnDeselectAll)
        MoveWindow(m_btnDeselectAll, x + MARGIN + BUTTON_WIDTH + 4, btnY,
            BUTTON_WIDTH + 10, BUTTON_HEIGHT, TRUE);

    // Reposition checkboxes
    int startY = y + TOP_OFFSET + BUTTON_HEIGHT + 6;
    for (int i = 0; i < static_cast<int>(m_collectors.size()); ++i)
    {
        int cbY = startY + i * (CHECKBOX_HEIGHT + CHECKBOX_SPACING);
        MoveWindow(m_collectors[i].checkbox, x + MARGIN, cbY,
            width - MARGIN * 2, CHECKBOX_HEIGHT, TRUE);
    }
}

} // namespace KTDiag
