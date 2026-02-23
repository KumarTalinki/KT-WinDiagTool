#pragma once

#include "../app/Config.h"

#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <atomic>

namespace KTDiag
{

class PerfController
{
public:
    PerfController();
    ~PerfController();

    // Start collecting perf counters from config at given interval.
    // Output CSV goes to outputDir/perf_logs/
    bool Start(const Config& config, int intervalMs,
               const std::filesystem::path& outputDir);

    // Stop collection and finalize CSV
    bool Stop();

    // Query state
    bool IsActive() const { return m_active; }
    std::wstring GetError() const { return m_error; }
    std::filesystem::path GetOutputPath() const { return m_csvPath; }

    // Non-copyable
    PerfController(const PerfController&) = delete;
    PerfController& operator=(const PerfController&) = delete;

private:
    void CollectThread();

    void* m_queryHandle = nullptr;         // PDH_HQUERY
    std::vector<void*> m_counterHandles;   // PDH_HCOUNTER
    std::vector<std::wstring> m_counterPaths;
    std::filesystem::path m_csvPath;
    int m_intervalMs = 1000;
    std::atomic<bool> m_stopFlag{false};
    std::thread m_thread;
    bool m_active = false;
    std::wstring m_error;
};

} // namespace KTDiag
