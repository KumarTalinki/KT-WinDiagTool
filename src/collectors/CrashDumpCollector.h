#pragma once

#include "ICollector.h"

namespace KTDiag
{

class CrashDumpCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Crash Dump Collection"; }
    std::wstring Id() const override { return L"dumps"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    struct DumpFileInfo
    {
        std::filesystem::path sourcePath;
        std::wstring sourceLabel;  // e.g. "minidump", "wer", "product", "registry"
        std::uintmax_t fileSize = 0;
        std::filesystem::file_time_type lastWrite;
    };

    // Gather all candidate dump paths from config, system locations, registry
    std::vector<std::wstring> GatherScanPaths(const Config& config);

    // Read additional dump directory path from a registry key
    std::wstring ReadRegistryDumpPath(const std::wstring& registryPath);

    // Scan a directory for .dmp files, applying age/size filters
    void ScanDirectory(const std::filesystem::path& dir,
                       const std::wstring& sourceLabel,
                       std::vector<DumpFileInfo>& results);

    // Write summary text file listing all found dumps
    void WriteSummary(const std::vector<DumpFileInfo>& dumps,
                      int copied,
                      const std::filesystem::path& outputFile);

    std::wstring m_error;
};

} // namespace KTDiag
