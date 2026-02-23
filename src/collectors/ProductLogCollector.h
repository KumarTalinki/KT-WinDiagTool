#pragma once

#include "ICollector.h"

namespace KTDiag
{

class ProductLogCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Product Logs"; }
    std::wstring Id() const override { return L"productlogs"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    // Recursively copy files from sourceDir to destDir
    int CopyDirectory(const std::filesystem::path& sourceDir,
                      const std::filesystem::path& destDir,
                      CancelToken cancel);

    std::wstring m_error;
};

} // namespace KTDiag
