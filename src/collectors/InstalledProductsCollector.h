#pragma once

#include "ICollector.h"

#include <Windows.h>
#include <fstream>

namespace KTDiag
{

class InstalledProductsCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Installed Products"; }
    std::wstring Id() const override { return L"installed"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    // Enumerate from a single Uninstall registry key (handles both 32-bit and 64-bit views)
    void EnumerateFromRegistry(HKEY rootKey, const std::wstring& subKey,
                               std::wofstream& file, int& count, CancelToken cancel);

    std::wstring m_error;
};

} // namespace KTDiag
