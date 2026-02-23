#pragma once

#include "ICollector.h"

#include <Windows.h>
#include <fstream>

namespace KTDiag
{

class ProxySettingsCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Proxy Settings"; }
    std::wstring Id() const override { return L"proxy"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    // Collect WinINET proxy settings from HKCU\...\Internet Settings
    void CollectWinInetProxy(std::wofstream& file);

    // Collect WinHTTP proxy settings from registry
    void CollectWinHttpProxy(std::wofstream& file);

    // Collect proxy-related environment variables
    void CollectEnvProxyVars(std::wofstream& file);

    // Read a DWORD from a registry key
    static DWORD ReadRegDword(HKEY hKey, const wchar_t* valueName, DWORD defaultVal = 0);

    // Read a string from a registry key
    static std::wstring ReadRegString(HKEY hKey, const wchar_t* valueName);

    std::wstring m_error;
};

} // namespace KTDiag
