#pragma once

#include "ICollector.h"

namespace KTDiag
{

class FirewallConfigCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Firewall Configuration"; }
    std::wstring Id() const override { return L"firewall"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    // Collect firewall profiles and rules via COM INetFwPolicy2
    bool CollectViaComApi(const std::filesystem::path& outputDir,
                          const Config& config,
                          CancelToken cancel);

    // Copy firewall log file if it exists
    void CopyFirewallLog(const std::filesystem::path& outputDir);

    std::wstring m_error;
};

} // namespace KTDiag
