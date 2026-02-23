#include "Privileges.h"
#include "Logger.h"

#pragma comment(lib, "advapi32.lib")

namespace KTDiag
{

bool Privileges::IsRunningAsAdmin()
{
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;

    if (AllocateAndInitializeSid(&ntAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &adminGroup))
    {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    return isAdmin != FALSE;
}

bool Privileges::EnableDebugPrivilege()
{
    bool result = EnablePrivilege(SE_DEBUG_NAME);
    if (result)
        Logger::Instance().Log(LogLevel::Info, L"SeDebugPrivilege enabled successfully");
    else
        Logger::Instance().Log(LogLevel::Warning, L"Failed to enable SeDebugPrivilege");
    return result;
}

bool Privileges::EnablePrivilege(const wchar_t* privilegeName)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!LookupPrivilegeValueW(nullptr, privilegeName, &tp.Privileges[0].Luid))
    {
        CloseHandle(token);
        return false;
    }

    BOOL result = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD lastError = GetLastError();
    CloseHandle(token);

    // AdjustTokenPrivileges returns TRUE even if not all privileges were set
    return result && lastError == ERROR_SUCCESS;
}

} // namespace KTDiag
