#include <Windows.h>
#include <sddl.h>
#include <lm.h>
#include <dsgetdc.h>
#include <dsrole.h>

#define SECURITY_WIN32
#include <security.h>

#include "UserInfoCollector.h"
#include "../util/Logger.h"

#include <fstream>
#include <vector>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "netapi32.lib")

namespace KTDiag
{

bool UserInfoCollector::Collect(const Config& config,
                                 const std::filesystem::path& outputDir,
                                 ProgressCallback cb,
                                 CancelToken cancel)
{
    UNREFERENCED_PARAMETER(config);
    if (cb) cb(Name(), L"Collecting user information...", 0);

    std::filesystem::path outFile = outputDir / L"user_info.txt";
    std::wofstream file(outFile);
    if (!file.is_open())
    {
        m_error = L"Failed to create output file: " + outFile.wstring();
        return false;
    }

    file << L"=== User Information ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    // Current user (SAM-compatible: DOMAIN\user)
    wchar_t samName[256] = {};
    ULONG samLen = _countof(samName);
    if (GetUserNameExW(NameSamCompatible, samName, &samLen))
        file << L"  SAM Name:       " << samName << std::endl;
    else
        file << L"  SAM Name:       (unavailable)" << std::endl;

    // Display name
    wchar_t displayName[256] = {};
    ULONG dispLen = _countof(displayName);
    if (GetUserNameExW(NameDisplay, displayName, &dispLen))
        file << L"  Display Name:   " << displayName << std::endl;

    // UPN (user@domain.com)
    wchar_t upn[256] = {};
    ULONG upnLen = _countof(upn);
    if (GetUserNameExW(NameUserPrincipal, upn, &upnLen))
        file << L"  UPN:            " << upn << std::endl;

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Looking up SID...", 20);

    // SID
    wchar_t userName[256] = {};
    DWORD userNameLen = _countof(userName);
    GetUserNameW(userName, &userNameLen);

    DWORD sidSize = 0, domainSize = 0;
    SID_NAME_USE sidUse;
    LookupAccountNameW(nullptr, userName, nullptr, &sidSize, nullptr, &domainSize, &sidUse);
    if (sidSize > 0)
    {
        std::vector<BYTE> sidBuf(sidSize);
        std::vector<wchar_t> domainBuf(domainSize);
        if (LookupAccountNameW(nullptr, userName,
                               sidBuf.data(), &sidSize,
                               domainBuf.data(), &domainSize, &sidUse))
        {
            LPWSTR sidStr = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<PSID>(sidBuf.data()), &sidStr))
            {
                file << L"  SID:            " << sidStr << std::endl;
                LocalFree(sidStr);
            }
            file << L"  Domain:         " << domainBuf.data() << std::endl;
        }
    }

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Checking group memberships...", 40);

    // Local group memberships
    file << std::endl << L"--- Local Group Memberships ---" << std::endl;
    LPLOCALGROUP_USERS_INFO_0 groups = nullptr;
    DWORD entriesRead = 0, totalEntries = 0;
    NET_API_STATUS status = NetUserGetLocalGroups(
        nullptr, userName, 0, LG_INCLUDE_INDIRECT,
        reinterpret_cast<LPBYTE*>(&groups),
        MAX_PREFERRED_LENGTH, &entriesRead, &totalEntries);

    if (status == NERR_Success && groups)
    {
        for (DWORD i = 0; i < entriesRead; ++i)
            file << L"  " << groups[i].lgrui0_name << std::endl;
        NetApiBufferFree(groups);
    }
    else
    {
        file << L"  (unable to enumerate, error=" << status << L")" << std::endl;
    }

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Checking domain join status...", 60);

    // Domain join status
    file << std::endl << L"--- Domain Join Status ---" << std::endl;
    LPWSTR joinName = nullptr;
    NETSETUP_JOIN_STATUS joinStatus = NetSetupUnknownStatus;
    if (NetGetJoinInformation(nullptr, &joinName, &joinStatus) == NERR_Success)
    {
        const wchar_t* statusStr = L"Unknown";
        switch (joinStatus)
        {
        case NetSetupUnjoined:       statusStr = L"Not joined"; break;
        case NetSetupWorkgroupName:  statusStr = L"Workgroup"; break;
        case NetSetupDomainName:     statusStr = L"Domain"; break;
        }
        file << L"  Join Type:      " << statusStr << std::endl;
        if (joinName)
        {
            file << L"  Join Name:      " << joinName << std::endl;
            NetApiBufferFree(joinName);
        }
    }

    // DNS domain name
    wchar_t dnsDomain[256] = {};
    DWORD dnsLen = _countof(dnsDomain);
    if (GetComputerNameExW(ComputerNameDnsDomain, dnsDomain, &dnsLen) && dnsLen > 0)
        file << L"  DNS Domain:     " << dnsDomain << std::endl;

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }

    // Domain controller info (if domain-joined)
    if (joinStatus == NetSetupDomainName)
    {
        if (cb) cb(Name(), L"Querying domain controller...", 80);

        file << std::endl << L"--- Active Directory ---" << std::endl;

        PDOMAIN_CONTROLLER_INFOW dcInfo = nullptr;
        DWORD dcResult = DsGetDcNameW(nullptr, nullptr, nullptr, nullptr,
                                       DS_RETURN_DNS_NAME, &dcInfo);
        if (dcResult == ERROR_SUCCESS && dcInfo)
        {
            if (dcInfo->DomainControllerName)
                file << L"  Domain Controller:  " << dcInfo->DomainControllerName << std::endl;
            if (dcInfo->DomainName)
                file << L"  Domain Name:        " << dcInfo->DomainName << std::endl;
            if (dcInfo->DnsForestName)
                file << L"  Forest Name:        " << dcInfo->DnsForestName << std::endl;
            if (dcInfo->ClientSiteName)
                file << L"  Client Site:        " << dcInfo->ClientSiteName << std::endl;
            if (dcInfo->DcSiteName)
                file << L"  DC Site:            " << dcInfo->DcSiteName << std::endl;

            NetApiBufferFree(dcInfo);
        }
        else
        {
            file << L"  Domain controller query failed (error=" << dcResult << L")" << std::endl;
        }
    }

    file << std::endl;
    file.close();

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[UserInfo] User information collected");
    return true;
}

} // namespace KTDiag
