#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace KTDiag
{

class Config;

class TraceLevelPanel
{
public:
    TraceLevelPanel();
    ~TraceLevelPanel();

    bool Create(HWND parentHwnd, HINSTANCE hInstance, int x, int y, int width, int height);
    void Resize(int x, int y, int width, int height);

    void UpdateFromConfig(const Config& config);

    // Returns WPP trace level 1-5 (Critical=1, Error=2, Warning=3, Info=4, Verbose=5)
    int GetWppTraceLevel() const;

    // Returns product log level string from combo selection
    std::wstring GetProductLogLevel() const;

    HWND GetHwnd() const { return m_groupBox; }

private:
    HWND m_groupBox = nullptr;
    HWND m_wppLabel = nullptr;
    HWND m_wppCombo = nullptr;
    HWND m_productLabel = nullptr;
    HWND m_productCombo = nullptr;
    HFONT m_font = nullptr;
};

} // namespace KTDiag
