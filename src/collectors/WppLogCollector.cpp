#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "WppLogCollector.h"
#include "../controllers/WppController.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <thread>
#include <chrono>

namespace
{

// Fallback used only if config returns 0 or negative
constexpr int DEFAULT_CAPTURE_SECONDS = 30;

void WaitWithCancelCheck(int durationSeconds, KTDiag::CancelToken cancel,
                         KTDiag::ProgressCallback cb,
                         const std::wstring& name,
                         int baseProgress, int maxProgress)
{
    for (int i = 0; i < durationSeconds; ++i)
    {
        if (cancel && cancel->load())
            return;

        int pct = baseProgress + ((i * (maxProgress - baseProgress)) / durationSeconds);
        if (cb) cb(name, L"Capturing WPP traces... (" +
                   std::to_wstring(durationSeconds - i) + L"s remaining)", pct);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // anonymous namespace

namespace KTDiag
{

bool WppLogCollector::Collect(const Config& config,
                               const std::filesystem::path& outputDir,
                               ProgressCallback cb,
                               CancelToken cancel)
{
    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    const auto& providers = config.GetWppProviders();
    if (providers.empty())
    {
        if (cb) cb(Name(), L"No WPP providers configured, skipping", 100);
        Logger::Instance().Log(LogLevel::Info, L"[WPP] No WPP providers in config, skipping");
        return true;
    }

    if (cb) cb(Name(), L"Starting WPP trace sessions...", 0);

    // Determine trace level from config default
    int traceLevel = 3; // Warning
    const auto& traceLevels = config.GetWppTraceLevels();
    if (traceLevels.defaultLevel == L"Critical")       traceLevel = 1;
    else if (traceLevels.defaultLevel == L"Error")     traceLevel = 2;
    else if (traceLevels.defaultLevel == L"Warning")   traceLevel = 3;
    else if (traceLevels.defaultLevel == L"Info")       traceLevel = 4;
    else if (traceLevels.defaultLevel == L"Verbose")   traceLevel = 5;

    WppController controller;
    if (!controller.Start(config, traceLevel, outputDir))
    {
        m_error = L"Failed to start WPP tracing: " + controller.GetError();
        Logger::Instance().Log(LogLevel::Error, L"[WPP] %s", m_error.c_str());
        return false;
    }

    if (cb) cb(Name(), L"WPP sessions started, capturing...", 10);

    int captureSecs = config.GetCaptureDurationSeconds();
    if (captureSecs <= 0) captureSecs = DEFAULT_CAPTURE_SECONDS;

    // Wait for capture duration
    WaitWithCancelCheck(captureSecs, cancel, cb, Name(), 10, 80);

    if (cancel && cancel->load())
    {
        controller.Stop();
        m_error = L"Cancelled";
        return false;
    }

    // Stop tracing
    if (cb) cb(Name(), L"Stopping WPP sessions...", 85);
    controller.Stop();

    // Report collected files
    const auto& etlPaths = controller.GetEtlPaths();
    if (cb) cb(Name(), L"Collected " + std::to_wstring(etlPaths.size()) + L" trace files", 95);

    for (const auto& path : etlPaths)
    {
        Logger::Instance().Log(LogLevel::Info,
            L"[WPP] Collected trace: %s", path.wstring().c_str());
    }

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info,
        L"[WPP] WPP trace collection complete (%zu files)", etlPaths.size());

    return true;
}

} // namespace KTDiag
