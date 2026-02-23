#pragma once

#include "ICollector.h"

namespace KTDiag
{

class SystemInfoCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"System Information"; }
    std::wstring Id() const override { return L"sysinfo"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    // Collect OS version info from registry (more reliable than GetVersionEx)
    void CollectOsInfo(std::wofstream& file);

    // Collect machine names
    void CollectMachineNames(std::wofstream& file);

    // Collect time and timezone
    void CollectTimeInfo(std::wofstream& file);

    // Collect hardware info via WMI
    void CollectHardwareInfo(std::wofstream& file);

    // Collect installed KBs via WMI
    bool CollectInstalledKBs(const std::filesystem::path& outputDir, CancelToken cancel);

    std::wstring m_error;
};

} // namespace KTDiag
