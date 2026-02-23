#pragma once

#include "ICollector.h"

namespace KTDiag
{

class NetworkConfigCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Network Configuration"; }
    std::wstring Id() const override { return L"network"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    // Collect adapter info via GetAdaptersAddresses
    bool CollectAdapters(const std::filesystem::path& outputDir, CancelToken cancel);

    // Collect routing table via GetIpForwardTable
    bool CollectRoutingTable(const std::filesystem::path& outputDir);

    // Collect active TCP/UDP connections with owning PIDs
    bool CollectActiveConnections(const std::filesystem::path& outputDir, CancelToken cancel);

    std::wstring m_error;
};

} // namespace KTDiag
