#pragma once

#include <Windows.h>

namespace KTDiag
{

class Privileges
{
public:
    // Check if the current process is running with admin privileges
    static bool IsRunningAsAdmin();

    // Enable SeDebugPrivilege for accessing crash dumps and process info
    static bool EnableDebugPrivilege();

    // Enable a specific privilege by name
    static bool EnablePrivilege(const wchar_t* privilegeName);
};

} // namespace KTDiag
