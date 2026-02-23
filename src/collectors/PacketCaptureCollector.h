#pragma once

#include <Windows.h>
#include "ICollector.h"

namespace KTDiag
{

class PacketCaptureCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Network Packet Capture"; }
    std::wstring Id() const override { return L"packets"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    // Run a command via CreateProcessW and capture output
    bool RunCommand(const std::wstring& cmdLine, std::wstring& output, DWORD timeoutMs);

    std::wstring m_error;
};

} // namespace KTDiag
