#pragma once

#include <Windows.h>

namespace KTDiag
{

class Application
{
public:
    explicit Application(HINSTANCE hInstance);
    ~Application();

    // Run the GUI application; returns exit code
    int Run(int nCmdShow);

    HINSTANCE GetInstance() const { return m_hInstance; }

private:
    HINSTANCE m_hInstance;
};

} // namespace KTDiag
