#pragma once

#include <Windows.h>
#include <string>
#include <functional>

namespace KTDiag
{

enum class ControllerType { Wpp = 0, Perf = 1, Etw = 2, Packet = 3 };
enum class ControllerState { Inactive, Starting, Active, Stopping };

class ControlPanel
{
public:
    ControlPanel();
    ~ControlPanel();

    bool Create(HWND parentHwnd, HINSTANCE hInstance, int x, int y, int width, int height);
    void Resize(int x, int y, int width, int height);

    void SetControllerState(ControllerType type, ControllerState state);
    ControllerState GetControllerState(ControllerType type) const;

    using ToggleCallback = std::function<void(ControllerType)>;
    void SetToggleCallback(ToggleCallback cb) { m_toggleCallback = std::move(cb); }

    bool OnCommand(WORD controlId, WORD notifyCode);

    HWND GetHwnd() const { return m_groupBox; }

private:
    struct ControllerRow
    {
        HWND label = nullptr;
        HWND toggleBtn = nullptr;
        HWND statusLabel = nullptr;
        ControllerState state = ControllerState::Inactive;
    };

    HWND m_groupBox = nullptr;
    HFONT m_font = nullptr;
    ControllerRow m_rows[4];
    ToggleCallback m_toggleCallback;

    void UpdateRowUI(int index);
};

} // namespace KTDiag
