#pragma once

#include <Windows.h>
#include "../app/Config.h"

#include <string>
#include <filesystem>

namespace KTDiag
{

class PacketCaptureController
{
public:
    PacketCaptureController();
    ~PacketCaptureController();

    // Start netsh packet capture. Output goes to outputDir/capture.etl
    bool Start(const Config& config, const std::filesystem::path& outputDir);

    // Stop the netsh trace session
    bool Stop();

    // Query state
    bool IsActive() const { return m_active; }
    std::wstring GetError() const { return m_error; }
    std::filesystem::path GetEtlPath() const { return m_etlPath; }

    // Non-copyable
    PacketCaptureController(const PacketCaptureController&) = delete;
    PacketCaptureController& operator=(const PacketCaptureController&) = delete;

private:
    bool RunCommand(const std::wstring& cmdLine, std::wstring& output, DWORD timeoutMs);

    std::filesystem::path m_etlPath;
    bool m_active = false;
    std::wstring m_error;
};

} // namespace KTDiag
