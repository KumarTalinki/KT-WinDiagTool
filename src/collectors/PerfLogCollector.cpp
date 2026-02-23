#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "PerfLogCollector.h"
#include "../controllers/PerfController.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <thread>
#include <chrono>

namespace
{

constexpr int DEFAULT_CAPTURE_SECONDS = 30;
constexpr int DEFAULT_INTERVAL_MS = 1000;

} // anonymous namespace

namespace KTDiag
{

bool PerfLogCollector::Collect(const Config& config,
                                const std::filesystem::path& outputDir,
                                ProgressCallback cb,
                                CancelToken cancel)
{
    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    const auto& counters = config.GetPerfCounters();
    if (counters.empty())
    {
        if (cb) cb(Name(), L"No performance counters configured, skipping", 100);
        Logger::Instance().Log(LogLevel::Info, L"[Perf] No perf counters in config, skipping");
        return true;
    }

    if (cb) cb(Name(), L"Starting performance counter collection...", 0);

    PerfController controller;
    if (!controller.Start(config, DEFAULT_INTERVAL_MS, outputDir))
    {
        m_error = L"Failed to start perf collection: " + controller.GetError();
        Logger::Instance().Log(LogLevel::Error, L"[Perf] %s", m_error.c_str());
        return false;
    }

    if (cb) cb(Name(), L"Collecting performance data...", 10);

    // Wait for capture duration
    for (int i = 0; i < DEFAULT_CAPTURE_SECONDS; ++i)
    {
        if (cancel && cancel->load())
        {
            controller.Stop();
            m_error = L"Cancelled";
            return false;
        }

        int pct = 10 + ((i * 75) / DEFAULT_CAPTURE_SECONDS);
        if (cb) cb(Name(), L"Collecting perf data... (" +
                   std::to_wstring(DEFAULT_CAPTURE_SECONDS - i) + L"s remaining)", pct);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (cb) cb(Name(), L"Stopping collection...", 90);
    controller.Stop();

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info,
        L"[Perf] Performance counter collection complete: %s",
        controller.GetOutputPath().wstring().c_str());

    return true;
}

} // namespace KTDiag
