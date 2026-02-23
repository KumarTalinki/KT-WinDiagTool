#include <Windows.h>
#include <objbase.h>
#include <netfw.h>
#include <oleauto.h>

#include "FirewallConfigCollector.h"
#include "../util/Logger.h"
#include "../util/StringUtils.h"

#include <vector>
#include <fstream>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace KTDiag
{

static const wchar_t* ProfileTypeToString(long profileType)
{
    switch (profileType)
    {
    case NET_FW_PROFILE2_DOMAIN:  return L"Domain";
    case NET_FW_PROFILE2_PRIVATE: return L"Private";
    case NET_FW_PROFILE2_PUBLIC:  return L"Public";
    default:                      return L"Unknown";
    }
}

static const wchar_t* ActionToString(NET_FW_ACTION action)
{
    switch (action)
    {
    case NET_FW_ACTION_BLOCK: return L"Block";
    case NET_FW_ACTION_ALLOW: return L"Allow";
    default:                  return L"Unknown";
    }
}

static const wchar_t* DirectionToString(NET_FW_RULE_DIRECTION dir)
{
    switch (dir)
    {
    case NET_FW_RULE_DIR_IN:  return L"Inbound";
    case NET_FW_RULE_DIR_OUT: return L"Outbound";
    default:                  return L"Unknown";
    }
}

static const wchar_t* ProtocolToString(long proto)
{
    switch (proto)
    {
    case 6:   return L"TCP";
    case 17:  return L"UDP";
    case 1:   return L"ICMPv4";
    case 58:  return L"ICMPv6";
    case 256: return L"Any";
    default:  return L"Other";
    }
}

static std::wstring BstrToWstring(BSTR bstr)
{
    if (!bstr) return L"";
    return std::wstring(bstr, SysStringLen(bstr));
}

bool FirewallConfigCollector::CollectViaComApi(const std::filesystem::path& outputDir,
                                                const Config& config,
                                                CancelToken cancel)
{
    // Initialize COM (may already be initialized on this thread)
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInitialized = SUCCEEDED(hrInit);

    INetFwPolicy2* pPolicy = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwPolicy2), reinterpret_cast<void**>(&pPolicy));
    if (FAILED(hr) || !pPolicy)
    {
        m_error = L"Failed to create INetFwPolicy2 (hr=0x" +
                  std::to_wstring(static_cast<unsigned long>(hr)) + L")";
        if (comInitialized) CoUninitialize();
        return false;
    }

    // --- Firewall Profiles ---
    {
        std::filesystem::path profilesFile = outputDir / L"firewall_profiles.txt";
        std::wofstream file(profilesFile);
        if (file.is_open())
        {
            file << L"=== Windows Firewall Profiles ===" << std::endl;
            file << L"========================================" << std::endl << std::endl;

            long currentProfiles = 0;
            pPolicy->get_CurrentProfileTypes(&currentProfiles);
            file << L"Active profile(s): ";
            bool first = true;
            if (currentProfiles & NET_FW_PROFILE2_DOMAIN)  { file << L"Domain"; first = false; }
            if (currentProfiles & NET_FW_PROFILE2_PRIVATE) { if (!first) file << L", "; file << L"Private"; first = false; }
            if (currentProfiles & NET_FW_PROFILE2_PUBLIC)  { if (!first) file << L", "; file << L"Public"; }
            file << std::endl << std::endl;

            long profiles[] = { NET_FW_PROFILE2_DOMAIN, NET_FW_PROFILE2_PRIVATE, NET_FW_PROFILE2_PUBLIC };
            for (long profile : profiles)
            {
                VARIANT_BOOL enabled = VARIANT_FALSE;
                pPolicy->get_FirewallEnabled(static_cast<NET_FW_PROFILE_TYPE2>(profile), &enabled);

                NET_FW_ACTION defaultInbound = NET_FW_ACTION_MAX;
                NET_FW_ACTION defaultOutbound = NET_FW_ACTION_MAX;
                pPolicy->get_DefaultInboundAction(static_cast<NET_FW_PROFILE_TYPE2>(profile), &defaultInbound);
                pPolicy->get_DefaultOutboundAction(static_cast<NET_FW_PROFILE_TYPE2>(profile), &defaultOutbound);

                VARIANT_BOOL notifications = VARIANT_FALSE;
                pPolicy->get_NotificationsDisabled(static_cast<NET_FW_PROFILE_TYPE2>(profile), &notifications);

                file << L"--- " << ProfileTypeToString(profile) << L" Profile ---" << std::endl;
                file << L"  Firewall Enabled:     " << (enabled == VARIANT_TRUE ? L"Yes" : L"No") << std::endl;
                file << L"  Default Inbound:      " << ActionToString(defaultInbound) << std::endl;
                file << L"  Default Outbound:     " << ActionToString(defaultOutbound) << std::endl;
                file << L"  Notifications Hidden: " << (notifications == VARIANT_TRUE ? L"Yes" : L"No") << std::endl;
                file << std::endl;
            }

            // Firewall logging settings from registry
            file << L"--- Logging Settings ---" << std::endl;
            HKEY hLogKey = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Policies\\Microsoft\\WindowsFirewall\\DomainProfile\\Logging",
                0, KEY_READ, &hLogKey) == ERROR_SUCCESS)
            {
                wchar_t logPath[MAX_PATH] = {};
                DWORD pathSize = sizeof(logPath);
                if (RegQueryValueExW(hLogKey, L"LogFilePath", nullptr, nullptr,
                                     reinterpret_cast<BYTE*>(logPath), &pathSize) == ERROR_SUCCESS)
                {
                    file << L"  Log File Path: " << logPath << std::endl;
                }
                RegCloseKey(hLogKey);
            }
            else
            {
                file << L"  Log File Path: %SystemRoot%\\system32\\LogFiles\\Firewall\\pfirewall.log (default)"
                     << std::endl;
            }
            file << std::endl;
            file.close();
        }
    }

    if (cancel && cancel->load())
    {
        pPolicy->Release();
        if (comInitialized) CoUninitialize();
        m_error = L"Cancelled";
        return false;
    }

    // --- Firewall Rules ---
    INetFwRules* pRules = nullptr;
    hr = pPolicy->get_Rules(&pRules);
    if (SUCCEEDED(hr) && pRules)
    {
        std::filesystem::path rulesFile = outputDir / L"firewall_rules.txt";
        std::wofstream file(rulesFile);

        // Also prepare product-filtered rules file
        const auto& svcNames = config.GetServiceNames();
        std::filesystem::path productRulesFile = outputDir / L"firewall_product_rules.txt";
        std::wofstream prodFile(productRulesFile);

        if (file.is_open())
        {
            file << L"=== Windows Firewall Rules ===" << std::endl;
            file << L"========================================" << std::endl << std::endl;
        }
        if (prodFile.is_open())
        {
            prodFile << L"=== Product-Related Firewall Rules ===" << std::endl;
            prodFile << L"Product: " << config.GetProductName() << std::endl;
            prodFile << L"========================================" << std::endl << std::endl;
        }

        long ruleCount = 0;
        pRules->get_Count(&ruleCount);

        IUnknown* pEnumUnk = nullptr;
        pRules->get__NewEnum(&pEnumUnk);
        IEnumVARIANT* pEnum = nullptr;
        if (pEnumUnk)
        {
            pEnumUnk->QueryInterface(__uuidof(IEnumVARIANT),
                                     reinterpret_cast<void**>(&pEnum));
            pEnumUnk->Release();
        }

        int totalRules = 0;
        int productRules = 0;
        if (pEnum)
        {
            VARIANT var;
            VariantInit(&var);
            ULONG fetched = 0;

            while (pEnum->Next(1, &var, &fetched) == S_OK && fetched == 1)
            {
                if (cancel && cancel->load())
                {
                    VariantClear(&var);
                    break;
                }

                INetFwRule* pRule = nullptr;
                if (var.vt == VT_DISPATCH && var.pdispVal)
                {
                    var.pdispVal->QueryInterface(__uuidof(INetFwRule),
                                                 reinterpret_cast<void**>(&pRule));
                }

                if (pRule)
                {
                    BSTR bstrName = nullptr;
                    VARIANT_BOOL enabled = VARIANT_FALSE;
                    NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_IN;
                    NET_FW_ACTION action = NET_FW_ACTION_BLOCK;
                    long protocol = 256;
                    BSTR bstrLocalPorts = nullptr;
                    BSTR bstrRemotePorts = nullptr;
                    BSTR bstrLocalAddrs = nullptr;
                    BSTR bstrRemoteAddrs = nullptr;
                    BSTR bstrAppPath = nullptr;
                    BSTR bstrServiceName = nullptr;
                    long profileMask = 0;

                    pRule->get_Name(&bstrName);
                    pRule->get_Enabled(&enabled);
                    pRule->get_Direction(&direction);
                    pRule->get_Action(&action);
                    pRule->get_Protocol(&protocol);
                    pRule->get_LocalPorts(&bstrLocalPorts);
                    pRule->get_RemotePorts(&bstrRemotePorts);
                    pRule->get_LocalAddresses(&bstrLocalAddrs);
                    pRule->get_RemoteAddresses(&bstrRemoteAddrs);
                    pRule->get_ApplicationName(&bstrAppPath);
                    pRule->get_ServiceName(&bstrServiceName);
                    pRule->get_Profiles(&profileMask);

                    std::wstring name = BstrToWstring(bstrName);
                    std::wstring appPath = BstrToWstring(bstrAppPath);
                    std::wstring svcName = BstrToWstring(bstrServiceName);
                    std::wstring localPorts = BstrToWstring(bstrLocalPorts);
                    std::wstring remotePorts = BstrToWstring(bstrRemotePorts);

                    // Write to main rules file
                    if (file.is_open())
                    {
                        file << name
                             << L"  |  " << (enabled == VARIANT_TRUE ? L"Enabled" : L"Disabled")
                             << L"  |  " << DirectionToString(direction)
                             << L"  |  " << ActionToString(action)
                             << L"  |  " << ProtocolToString(protocol);
                        if (!localPorts.empty())
                            file << L"  |  Local: " << localPorts;
                        if (!remotePorts.empty())
                            file << L"  |  Remote: " << remotePorts;
                        if (!appPath.empty())
                            file << L"  |  App: " << appPath;
                        if (!svcName.empty())
                            file << L"  |  Svc: " << svcName;
                        file << std::endl;
                    }
                    ++totalRules;

                    // Check if this rule matches the product
                    bool isProductRule = false;
                    std::wstring productName = config.GetProductName();
                    if (!productName.empty())
                    {
                        // Match by rule name containing product name
                        if (name.find(productName) != std::wstring::npos)
                            isProductRule = true;
                        // Match by app path containing product name
                        if (!appPath.empty() && appPath.find(productName) != std::wstring::npos)
                            isProductRule = true;
                    }
                    // Match by configured service names
                    for (const auto& cfgSvc : svcNames)
                    {
                        if (!svcName.empty() && StringUtils::EqualsIgnoreCase(svcName, cfgSvc))
                        {
                            isProductRule = true;
                            break;
                        }
                    }

                    if (isProductRule && prodFile.is_open())
                    {
                        prodFile << name
                                 << L"  |  " << (enabled == VARIANT_TRUE ? L"Enabled" : L"Disabled")
                                 << L"  |  " << DirectionToString(direction)
                                 << L"  |  " << ActionToString(action)
                                 << L"  |  " << ProtocolToString(protocol);
                        if (!localPorts.empty())
                            prodFile << L"  |  Local: " << localPorts;
                        if (!remotePorts.empty())
                            prodFile << L"  |  Remote: " << remotePorts;
                        if (!appPath.empty())
                            prodFile << L"  |  App: " << appPath;
                        if (!svcName.empty())
                            prodFile << L"  |  Svc: " << svcName;
                        prodFile << std::endl;
                        ++productRules;
                    }

                    SysFreeString(bstrName);
                    SysFreeString(bstrLocalPorts);
                    SysFreeString(bstrRemotePorts);
                    SysFreeString(bstrLocalAddrs);
                    SysFreeString(bstrRemoteAddrs);
                    SysFreeString(bstrAppPath);
                    SysFreeString(bstrServiceName);
                    pRule->Release();
                }

                VariantClear(&var);
            }

            pEnum->Release();
        }

        if (file.is_open())
        {
            file << std::endl << L"========================================" << std::endl;
            file << L"Total rules: " << totalRules << std::endl;
            file.close();
        }
        if (prodFile.is_open())
        {
            prodFile << std::endl << L"========================================" << std::endl;
            prodFile << L"Product-related rules: " << productRules << std::endl;
            prodFile.close();
        }

        pRules->Release();
        Logger::Instance().Log(LogLevel::Info,
            L"[Firewall] Enumerated %d rules (%d product-related)", totalRules, productRules);
    }

    pPolicy->Release();
    if (comInitialized) CoUninitialize();
    return true;
}

void FirewallConfigCollector::CopyFirewallLog(const std::filesystem::path& outputDir)
{
    // Default firewall log location
    wchar_t sysRoot[MAX_PATH] = {};
    GetEnvironmentVariableW(L"SystemRoot", sysRoot, MAX_PATH);
    std::filesystem::path logPath = std::filesystem::path(sysRoot) /
        L"system32" / L"LogFiles" / L"Firewall" / L"pfirewall.log";

    std::error_code ec;
    if (std::filesystem::exists(logPath, ec))
    {
        std::filesystem::path dest = outputDir / L"pfirewall.log";
        std::filesystem::copy_file(logPath, dest,
                                    std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[Firewall] Failed to copy firewall log: %s", ec.message().c_str());
        }
        else
        {
            Logger::Instance().Log(LogLevel::Info, L"[Firewall] Copied pfirewall.log");
        }
    }
    else
    {
        Logger::Instance().Log(LogLevel::Info, L"[Firewall] pfirewall.log not found (logging may be disabled)");
    }
}

bool FirewallConfigCollector::Collect(const Config& config,
                                       const std::filesystem::path& outputDir,
                                       ProgressCallback cb,
                                       CancelToken cancel)
{
    if (cb) cb(Name(), L"Querying firewall profiles and rules...", 0);

    if (!CollectViaComApi(outputDir, config, cancel))
        return false;

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Copying firewall log...", 80);
    CopyFirewallLog(outputDir);

    if (cb) cb(Name(), L"Done", 100);
    return true;
}

} // namespace KTDiag
