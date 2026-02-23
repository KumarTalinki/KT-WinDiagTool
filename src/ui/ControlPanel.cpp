#include "ControlPanel.h"
#include "resource.h"

namespace KTDiag
{

struct ControllerDef
{
    const wchar_t* name;
    WORD toggleId;
    WORD statusId;
};

static const ControllerDef g_controllerDefs[] = {
    { L"WPP Tracing:",    IDC_WPP_TOGGLE,    IDC_WPP_STATUS },
    { L"Perf Logging:",   IDC_PERF_TOGGLE,   IDC_PERF_STATUS },
    { L"ETW Tracing:",    IDC_ETW_TOGGLE,    IDC_ETW_STATUS },
    { L"Packet Capture:", IDC_PACKET_TOGGLE,  IDC_PACKET_STATUS },
};

static constexpr int ROW_HEIGHT = 28;
static constexpr int MARGIN = 8;
static constexpr int TOP_OFFSET = 18;
static constexpr int LABEL_WIDTH = 100;
static constexpr int BTN_WIDTH = 60;
static constexpr int BTN_HEIGHT = 22;

ControlPanel::ControlPanel() = default;

ControlPanel::~ControlPanel()
{
    if (m_font)
        DeleteObject(m_font);
}

bool ControlPanel::Create(HWND parentHwnd, HINSTANCE hInstance, int x, int y, int width, int height)
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_font = CreateFontIndirectW(&ncm.lfMessageFont);

    m_groupBox = CreateWindowExW(0, L"BUTTON", L"Trace Controllers",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, width, height,
        parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CTRL_GROUP)),
        hInstance, nullptr);
    SendMessageW(m_groupBox, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    for (int i = 0; i < 4; ++i)
    {
        int rowY = y + TOP_OFFSET + i * ROW_HEIGHT;
        int colX = x + MARGIN;

        // Label
        m_rows[i].label = CreateWindowExW(0, L"STATIC", g_controllerDefs[i].name,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            colX, rowY + 3, LABEL_WIDTH, 16,
            parentHwnd, nullptr, hInstance, nullptr);
        SendMessageW(m_rows[i].label, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

        // Toggle button
        m_rows[i].toggleBtn = CreateWindowExW(0, L"BUTTON", L"Start",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            colX + LABEL_WIDTH + 4, rowY, BTN_WIDTH, BTN_HEIGHT,
            parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(g_controllerDefs[i].toggleId)),
            hInstance, nullptr);
        SendMessageW(m_rows[i].toggleBtn, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

        // Status label
        m_rows[i].statusLabel = CreateWindowExW(0, L"STATIC", L"Inactive",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            colX + LABEL_WIDTH + BTN_WIDTH + 12, rowY + 3,
            width - LABEL_WIDTH - BTN_WIDTH - MARGIN * 2 - 16, 16,
            parentHwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(g_controllerDefs[i].statusId)),
            hInstance, nullptr);
        SendMessageW(m_rows[i].statusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

        m_rows[i].state = ControllerState::Inactive;
    }

    return true;
}

void ControlPanel::SetControllerState(ControllerType type, ControllerState state)
{
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= 4) return;

    m_rows[idx].state = state;
    UpdateRowUI(idx);
}

ControllerState ControlPanel::GetControllerState(ControllerType type) const
{
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= 4) return ControllerState::Inactive;
    return m_rows[idx].state;
}

void ControlPanel::UpdateRowUI(int index)
{
    if (index < 0 || index >= 4) return;

    const auto& row = m_rows[index];
    switch (row.state)
    {
    case ControllerState::Inactive:
        SetWindowTextW(row.toggleBtn, L"Start");
        EnableWindow(row.toggleBtn, TRUE);
        SetWindowTextW(row.statusLabel, L"Inactive");
        break;
    case ControllerState::Starting:
        SetWindowTextW(row.toggleBtn, L"Stop");
        EnableWindow(row.toggleBtn, FALSE);
        SetWindowTextW(row.statusLabel, L"Starting...");
        break;
    case ControllerState::Active:
        SetWindowTextW(row.toggleBtn, L"Stop");
        EnableWindow(row.toggleBtn, TRUE);
        SetWindowTextW(row.statusLabel, L"Active");
        break;
    case ControllerState::Stopping:
        SetWindowTextW(row.toggleBtn, L"Start");
        EnableWindow(row.toggleBtn, FALSE);
        SetWindowTextW(row.statusLabel, L"Stopping...");
        break;
    }
}

bool ControlPanel::OnCommand(WORD controlId, WORD notifyCode)
{
    UNREFERENCED_PARAMETER(notifyCode);

    for (int i = 0; i < 4; ++i)
    {
        if (controlId == g_controllerDefs[i].toggleId)
        {
            if (m_toggleCallback)
                m_toggleCallback(static_cast<ControllerType>(i));
            return true;
        }
    }
    return false;
}

void ControlPanel::Resize(int x, int y, int width, int height)
{
    if (m_groupBox)
        MoveWindow(m_groupBox, x, y, width, height, TRUE);

    for (int i = 0; i < 4; ++i)
    {
        int rowY = y + TOP_OFFSET + i * ROW_HEIGHT;
        int colX = x + MARGIN;

        if (m_rows[i].label)
            MoveWindow(m_rows[i].label, colX, rowY + 3, LABEL_WIDTH, 16, TRUE);
        if (m_rows[i].toggleBtn)
            MoveWindow(m_rows[i].toggleBtn, colX + LABEL_WIDTH + 4, rowY, BTN_WIDTH, BTN_HEIGHT, TRUE);
        if (m_rows[i].statusLabel)
            MoveWindow(m_rows[i].statusLabel, colX + LABEL_WIDTH + BTN_WIDTH + 12, rowY + 3,
                width - LABEL_WIDTH - BTN_WIDTH - MARGIN * 2 - 16, 16, TRUE);
    }
}

} // namespace KTDiag
