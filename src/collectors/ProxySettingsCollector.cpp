#include "ProxySettingsCollector.h"
#include "../util/Logger.h"

#include <vector>

namespace KTDiag
{

DWORD ProxySettingsCollector::ReadRegDword(HKEY hKey, const wchar_t* valueName, DWORD defaultVal)
{
    DWORD value = 0, size = sizeof(DWORD), type = 0;
    if (RegQueryValueExW(hKey, valueName, nullptr, &type,
                         reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
    {
        return value;
    }
    return defaultVal;
}

std::wstring ProxySettingsCollector::ReadRegString(HKEY hKey, const wchar_t* valueName)
{
    DWORD type = 0, dataSize = 0;
    if (RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &dataSize) != ERROR_SUCCESS)
        return L"";
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return L"";

    std::wstring result(dataSize / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(hKey, valueName, nullptr, nullptr,
                         reinterpret_cast<BYTE*>(&result[0]), &dataSize) != ERROR_SUCCESS)
        return L"";

    while (!result.empty() && result.back() == L'\0')
        result.pop_back();

    return result;
}

void ProxySettingsCollector::CollectWinInetProxy(std::wofstream& file)
{
    file << L"--- WinINET Proxy (User-Level) ---" << std::endl;

    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
        0, KEY_READ, &hKey);

    if (rc != ERROR_SUCCESS)
    {
        file << L"  Unable to open Internet Settings registry key (error=" << rc << L")"
             << std::endl << std::endl;
        return;
    }

    DWORD proxyEnable = ReadRegDword(hKey, L"ProxyEnable", 0);
    std::wstring proxyServer = ReadRegString(hKey, L"ProxyServer");
    std::wstring proxyOverride = ReadRegString(hKey, L"ProxyOverride");
    std::wstring autoConfigUrl = ReadRegString(hKey, L"AutoConfigURL");
    DWORD autoDetect = ReadRegDword(hKey, L"AutoDetect", 0);

    file << L"  Proxy Enabled:    " << (proxyEnable ? L"Yes" : L"No") << std::endl;
    file << L"  Proxy Server:     " << (proxyServer.empty() ? L"(not set)" : proxyServer)
         << std::endl;
    file << L"  Proxy Bypass:     " << (proxyOverride.empty() ? L"(not set)" : proxyOverride)
         << std::endl;
    file << L"  Auto-Config URL:  " << (autoConfigUrl.empty() ? L"(not set)" : autoConfigUrl)
         << std::endl;
    file << L"  Auto-Detect:      " << (autoDetect ? L"Yes" : L"No") << std::endl;

    // Flag common issues
    if (proxyEnable && proxyServer.empty())
        file << L"  ** WARNING: Proxy is enabled but no proxy server is configured **" << std::endl;
    if (proxyEnable && !proxyOverride.empty() &&
        proxyOverride.find(L"localhost") == std::wstring::npos &&
        proxyOverride.find(L"<local>") == std::wstring::npos)
    {
        file << L"  ** NOTE: Proxy bypass list does not include localhost or <local> **"
             << std::endl;
    }

    RegCloseKey(hKey);
    file << std::endl;
}

void ProxySettingsCollector::CollectWinHttpProxy(std::wofstream& file)
{
    file << L"--- WinHTTP Proxy (System-Level) ---" << std::endl;

    // Read WinHTTP settings from registry
    // The binary blob at WinHttpSettings encodes proxy info, but parsing it is complex.
    // We'll read the well-known DefaultConnectionSettings instead and also check
    // the machine-level Internet Settings.
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\Connections",
        0, KEY_READ, &hKey);

    if (rc == ERROR_SUCCESS)
    {
        // Check if WinHttpSettings blob exists
        DWORD dataSize = 0;
        DWORD type = 0;
        rc = RegQueryValueExW(hKey, L"WinHttpSettings", nullptr, &type, nullptr, &dataSize);
        if (rc == ERROR_SUCCESS && dataSize > 0)
        {
            std::vector<BYTE> data(dataSize);
            if (RegQueryValueExW(hKey, L"WinHttpSettings", nullptr, nullptr,
                                 data.data(), &dataSize) == ERROR_SUCCESS)
            {
                // WinHttpSettings binary format:
                // Offset 8: DWORD flags (bit 0 = manual proxy, bit 2 = auto-detect, bit 3 = auto-config)
                // Followed by: proxy string length, proxy string, bypass string length, bypass string
                if (dataSize >= 12)
                {
                    DWORD flags = *reinterpret_cast<DWORD*>(data.data() + 8);
                    bool manualProxy = (flags & 0x01) != 0;
                    bool autoDetect = (flags & 0x08) != 0;
                    bool autoConfig = (flags & 0x04) != 0;

                    file << L"  Manual Proxy:  " << (manualProxy ? L"Yes" : L"No") << std::endl;
                    file << L"  Auto-Detect:   " << (autoDetect ? L"Yes" : L"No") << std::endl;
                    file << L"  Auto-Config:   " << (autoConfig ? L"Yes" : L"No") << std::endl;

                    // Parse proxy and bypass strings if manual proxy is set
                    if (manualProxy && dataSize > 16)
                    {
                        DWORD proxyLen = *reinterpret_cast<DWORD*>(data.data() + 12);
                        if (proxyLen > 0 && 16 + proxyLen <= dataSize)
                        {
                            std::string proxyA(reinterpret_cast<char*>(data.data() + 16), proxyLen);
                            std::wstring proxy(proxyA.begin(), proxyA.end());
                            file << L"  Proxy Server:  " << proxy << std::endl;

                            DWORD offset = 16 + proxyLen;
                            if (offset + 4 <= dataSize)
                            {
                                DWORD bypassLen = *reinterpret_cast<DWORD*>(data.data() + offset);
                                if (bypassLen > 0 && offset + 4 + bypassLen <= dataSize)
                                {
                                    std::string bypassA(
                                        reinterpret_cast<char*>(data.data() + offset + 4),
                                        bypassLen);
                                    std::wstring bypass(bypassA.begin(), bypassA.end());
                                    file << L"  Proxy Bypass:  " << bypass << std::endl;
                                }
                            }
                        }
                    }
                }
                else
                {
                    file << L"  WinHttpSettings blob too small (" << dataSize << L" bytes)"
                         << std::endl;
                }
            }
        }
        else
        {
            file << L"  WinHttpSettings: (not configured)" << std::endl;
        }

        RegCloseKey(hKey);
    }
    else
    {
        file << L"  Unable to read WinHTTP connections registry key" << std::endl;
    }

    // Also check WPAD auto-proxy service status
    HKEY hSvcKey = nullptr;
    rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\WinHttpAutoProxySvc",
        0, KEY_READ, &hSvcKey);
    if (rc == ERROR_SUCCESS)
    {
        DWORD start = ReadRegDword(hSvcKey, L"Start", 0);
        const wchar_t* startStr = L"Unknown";
        switch (start)
        {
        case 2: startStr = L"Automatic"; break;
        case 3: startStr = L"Manual"; break;
        case 4: startStr = L"Disabled"; break;
        }
        file << L"  WPAD Service (WinHttpAutoProxySvc): " << startStr << std::endl;
        RegCloseKey(hSvcKey);
    }

    file << std::endl;
}

void ProxySettingsCollector::CollectEnvProxyVars(std::wofstream& file)
{
    file << L"--- Environment Variable Proxies ---" << std::endl;

    const wchar_t* varNames[] = {
        L"HTTP_PROXY", L"HTTPS_PROXY", L"FTP_PROXY", L"NO_PROXY",
        L"http_proxy", L"https_proxy", L"ftp_proxy", L"no_proxy",
        L"ALL_PROXY", L"all_proxy"
    };

    bool anyFound = false;
    for (const auto* varName : varNames)
    {
        wchar_t buf[2048] = {};
        DWORD len = GetEnvironmentVariableW(varName, buf, _countof(buf));
        if (len > 0)
        {
            file << L"  " << varName << L" = " << buf << std::endl;
            anyFound = true;
        }
    }

    if (!anyFound)
        file << L"  (no proxy environment variables set)" << std::endl;

    file << std::endl;
}

bool ProxySettingsCollector::Collect(const Config& config,
                                      const std::filesystem::path& outputDir,
                                      ProgressCallback cb,
                                      CancelToken cancel)
{
    UNREFERENCED_PARAMETER(config);
    if (cb) cb(Name(), L"Collecting proxy settings...", 0);

    std::filesystem::path outFile = outputDir / L"proxy_settings.txt";
    std::wofstream file(outFile);
    if (!file.is_open())
    {
        m_error = L"Failed to create output file: " + outFile.wstring();
        return false;
    }

    file << L"=== Proxy Settings ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Reading WinINET proxy...", 10);
    CollectWinInetProxy(file);

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Reading WinHTTP proxy...", 40);
    CollectWinHttpProxy(file);

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Reading environment variables...", 70);
    CollectEnvProxyVars(file);

    file.close();

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[Proxy] Collected proxy settings");
    return true;
}

} // namespace KTDiag
