#pragma once

#include "../app/Config.h"

#include <string>
#include <vector>
#include <filesystem>

namespace KTDiag
{

class WppController
{
public:
    WppController();
    ~WppController();

    // Start WPP tracing for all providers in config at given level (1-5).
    // Output .etl files go to outputDir/wpp_traces/
    bool Start(const Config& config, int traceLevel,
               const std::filesystem::path& outputDir);

    // Stop all active WPP trace sessions
    bool Stop();

    // Query state
    bool IsActive() const { return m_active; }
    int CurrentLevel() const { return m_traceLevel; }
    std::wstring GetError() const { return m_error; }

    // Get paths to .etl files produced (valid after Stop)
    const std::vector<std::filesystem::path>& GetEtlPaths() const { return m_etlPaths; }

    // Product log level control (registry-based)
    static bool SetProductLogLevel(const Config& config, const std::wstring& level);
    static std::wstring GetProductLogLevel(const Config& config);

    // Non-copyable
    WppController(const WppController&) = delete;
    WppController& operator=(const WppController&) = delete;

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
    int m_traceLevel = 3;
    std::wstring m_error;
};

} // namespace KTDiag
