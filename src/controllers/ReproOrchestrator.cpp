#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "ReproOrchestrator.h"
#include "WppController.h"
#include "EtwController.h"
#include "PerfController.h"
#include "PacketCaptureController.h"
#include "../util/Logger.h"

namespace KTDiag
{

ReproOrchestrator::ReproOrchestrator() = default;

ReproOrchestrator::~ReproOrchestrator()
{
    if (IsActive())
        StopRepro();
}

ReproResult ReproOrchestrator::StartRepro(const Config& config,
                                           const std::filesystem::path& outputDir,
                                           int wppLevel,
                                           int perfIntervalMs)
{
    ReproResult result;
    m_startTime = std::chrono::system_clock::now();

    Logger::Instance().Log(LogLevel::Info, L"[Repro] Starting repro mode...");

    // 1. WPP tracing (if providers configured)
    if (!config.GetWppProviders().empty())
    {
        m_wppController = std::make_unique<WppController>();
        if (m_wppController->Start(config, wppLevel, outputDir))
        {
            result.wppStarted = true;
            Logger::Instance().Log(LogLevel::Info,
                L"[Repro] WPP tracing started (level %d)", wppLevel);
        }
        else
        {
            result.errors.push_back(L"WPP: " + m_wppController->GetError());
            Logger::Instance().Log(LogLevel::Warning,
                L"[Repro] WPP start failed: %s", m_wppController->GetError().c_str());
            m_wppController.reset();
        }
    }
    else
    {
        Logger::Instance().Log(LogLevel::Info,
            L"[Repro] No WPP providers configured, skipping WPP");
    }

    // 2. ETW sessions (always — has built-in kernel providers)
    m_etwController = std::make_unique<EtwController>();
    if (m_etwController->Start(config, outputDir))
    {
        result.etwStarted = true;
        Logger::Instance().Log(LogLevel::Info, L"[Repro] ETW tracing started");
    }
    else
    {
        result.errors.push_back(L"ETW: " + m_etwController->GetError());
        Logger::Instance().Log(LogLevel::Warning,
            L"[Repro] ETW start failed: %s", m_etwController->GetError().c_str());
        m_etwController.reset();
    }

    // 3. Performance counters (if counters configured)
    if (!config.GetPerfCounters().empty())
    {
        m_perfController = std::make_unique<PerfController>();
        if (m_perfController->Start(config, perfIntervalMs, outputDir))
        {
            result.perfStarted = true;
            Logger::Instance().Log(LogLevel::Info,
                L"[Repro] Performance logging started (%dms interval)", perfIntervalMs);
        }
        else
        {
            result.errors.push_back(L"Perf: " + m_perfController->GetError());
            Logger::Instance().Log(LogLevel::Warning,
                L"[Repro] Perf start failed: %s", m_perfController->GetError().c_str());
            m_perfController.reset();
        }
    }
    else
    {
        Logger::Instance().Log(LogLevel::Info,
            L"[Repro] No perf counters configured, skipping performance logging");
    }

    // 4. Packet capture (always attempt — useful regardless of config)
    m_packetController = std::make_unique<PacketCaptureController>();
    if (m_packetController->Start(config, outputDir))
    {
        result.packetStarted = true;
        Logger::Instance().Log(LogLevel::Info, L"[Repro] Packet capture started");
    }
    else
    {
        result.errors.push_back(L"Packets: " + m_packetController->GetError());
        Logger::Instance().Log(LogLevel::Warning,
            L"[Repro] Packet capture start failed: %s",
            m_packetController->GetError().c_str());
        m_packetController.reset();
    }

    int started = (result.wppStarted ? 1 : 0) + (result.etwStarted ? 1 : 0) +
                  (result.perfStarted ? 1 : 0) + (result.packetStarted ? 1 : 0);

    Logger::Instance().Log(LogLevel::Info,
        L"[Repro] Repro mode started: %d/4 trace types active", started);

    return result;
}

ReproResult ReproOrchestrator::StopRepro()
{
    ReproResult result;
    m_stopTime = std::chrono::system_clock::now();

    Logger::Instance().Log(LogLevel::Info, L"[Repro] Stopping repro mode...");

    // Stop in reverse order: Packets → Perf → ETW → WPP

    // 1. Packet capture
    if (m_packetController && m_packetController->IsActive())
    {
        if (m_packetController->Stop())
        {
            result.packetStarted = true; // reuse field to mean "stopped OK"
            Logger::Instance().Log(LogLevel::Info, L"[Repro] Packet capture stopped");
        }
        else
        {
            result.errors.push_back(L"Packets stop: " + m_packetController->GetError());
        }
    }

    // 2. Performance counters
    if (m_perfController && m_perfController->IsActive())
    {
        if (m_perfController->Stop())
        {
            result.perfStarted = true;
            Logger::Instance().Log(LogLevel::Info, L"[Repro] Performance logging stopped");
        }
        else
        {
            result.errors.push_back(L"Perf stop: " + m_perfController->GetError());
        }
    }

    // 3. ETW
    if (m_etwController && m_etwController->IsActive())
    {
        if (m_etwController->Stop())
        {
            result.etwStarted = true;
            Logger::Instance().Log(LogLevel::Info, L"[Repro] ETW tracing stopped");
        }
        else
        {
            result.errors.push_back(L"ETW stop: " + m_etwController->GetError());
        }
    }

    // 4. WPP
    if (m_wppController && m_wppController->IsActive())
    {
        if (m_wppController->Stop())
        {
            result.wppStarted = true;
            Logger::Instance().Log(LogLevel::Info, L"[Repro] WPP tracing stopped");
        }
        else
        {
            result.errors.push_back(L"WPP stop: " + m_wppController->GetError());
        }
    }

    Logger::Instance().Log(LogLevel::Info, L"[Repro] Repro mode stopped");
    return result;
}

bool ReproOrchestrator::IsActive() const
{
    if (m_wppController && m_wppController->IsActive()) return true;
    if (m_etwController && m_etwController->IsActive()) return true;
    if (m_perfController && m_perfController->IsActive()) return true;
    if (m_packetController && m_packetController->IsActive()) return true;
    return false;
}

} // namespace KTDiag
