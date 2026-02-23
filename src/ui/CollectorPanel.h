#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <set>

namespace KTDiag
{

struct CollectorInfo
{
    std::wstring id;
    std::wstring name;
    HWND checkbox = nullptr;
};

class CollectorPanel
{
public:
    CollectorPanel();
    ~CollectorPanel();

    bool Create(HWND parentHwnd, HINSTANCE hInstance, int x, int y, int width, int height);
    void Resize(int x, int y, int width, int height);

    void InitializeCollectors();
    std::set<std::wstring> GetSelectedIds() const;
    void SelectAll(bool select);

    bool OnCommand(WORD controlId, WORD notifyCode);

    HWND GetHwnd() const { return m_groupBox; }

private:
    HWND m_groupBox = nullptr;
    HWND m_btnSelectAll = nullptr;
    HWND m_btnDeselectAll = nullptr;
    HFONT m_font = nullptr;
    HINSTANCE m_hInstance = nullptr;
    HWND m_parent = nullptr;
    std::vector<CollectorInfo> m_collectors;
};

} // namespace KTDiag
