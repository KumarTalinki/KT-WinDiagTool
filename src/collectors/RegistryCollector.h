#pragma once

#include "ICollector.h"

#include <Windows.h>
#include <fstream>

namespace KTDiag
{

class RegistryCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Registry Keys"; }
    std::wstring Id() const override { return L"registry"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    // Parse "HKLM\SOFTWARE\..." into root key handle + subkey path
    static bool ParseRegistryPath(const std::wstring& fullPath,
                                  HKEY& rootKey, std::wstring& subKeyPath);

    // Recursively export a registry key and its subkeys
    void ExportKey(HKEY rootKey, const std::wstring& subKeyPath,
                   const std::wstring& displayPath,
                   std::wofstream& file, int depth, CancelToken cancel);

    // Format a registry value for display
    static std::wstring FormatRegValue(DWORD type, const BYTE* data, DWORD dataSize);

    std::wstring m_error;
};

} // namespace KTDiag
