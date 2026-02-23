#pragma once

#include "../app/Config.h"

#include <string>
#include <vector>
#include <filesystem>

namespace KTDiag
{

class EtwController
{
public:
    EtwController();
    ~EtwController();

    // Start ETW sessions for all config providers + built-in I/O providers.
    // Output .etl files go to outputDir/etw/
    bool Start(const Config& config, const std::filesystem::path& outputDir);

    // Stop all active ETW trace sessions
    bool Stop();

    // Query state
    bool IsActive() const { return m_active; }
    std::wstring GetError() const { return m_error; }

    // Get paths to .etl files produced (valid after Stop)
    const std::vector<std::filesystem::path>& GetEtlPaths() const { return m_etlPaths; }

    // Non-copyable
    EtwController(const EtwController&) = delete;
    EtwController& operator=(const EtwController&) = delete;

private:
    struct SessionInfo
    {
        unsigned __int64 handle;    // TRACEHANDLE
        std::wstring sessionName;
        std::filesystem::path etlPath;
    };

    std::vector<SessionInfo> m_sessions;
    std::vector<std::filesystem::path> m_etlPaths;
    bool m_active = false;
    std::wstring m_error;
};

} // namespace KTDiag
