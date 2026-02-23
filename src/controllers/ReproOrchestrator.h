#pragma once

#include "../app/Config.h"

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <chrono>

namespace KTDiag
{

class WppController;
class EtwController;
class PerfController;
class PacketCaptureController;

struct ReproResult
{
    bool wppStarted = false;
    bool etwStarted = false;
    bool perfStarted = false;
    bool packetStarted = false;
    std::vector<std::wstring> errors;
};

class ReproOrchestrator
{
public:
    ReproOrchestrator();
    ~ReproOrchestrator();

    // Start all background traces/captures.
    // wppLevel: 1-5 (CRITICAL..VERBOSE)
    // perfIntervalMs: perf counter sampling interval
    ReproResult StartRepro(const Config& config,
                           const std::filesystem::path& outputDir,
                           int wppLevel,
                           int perfIntervalMs = 1000);

    // Stop all active traces/captures
    ReproResult StopRepro();

    // Query overall state — true if any controller is active
    bool IsActive() const;

    // Get start/stop timestamps (for future event log time filtering)
    std::chrono::system_clock::time_point GetStartTime() const { return m_startTime; }
    std::chrono::system_clock::time_point GetStopTime() const { return m_stopTime; }

    // Non-copyable
    ReproOrchestrator(const ReproOrchestrator&) = delete;
    ReproOrchestrator& operator=(const ReproOrchestrator&) = delete;

private:
    std::unique_ptr<WppController> m_wppController;
    std::unique_ptr<EtwController> m_etwController;
    std::unique_ptr<PerfController> m_perfController;
    std::unique_ptr<PacketCaptureController> m_packetController;
    std::chrono::system_clock::time_point m_startTime;
    std::chrono::system_clock::time_point m_stopTime;
};

} // namespace KTDiag
