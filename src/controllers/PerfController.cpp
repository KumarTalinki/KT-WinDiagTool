#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "PerfController.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <Pdh.h>
#include <PdhMsg.h>
#include <fstream>
#include <chrono>
#include <cstdio>

#pragma comment(lib, "pdh.lib")

namespace
{

std::wstring GetTimestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// Expand wildcard counter paths (e.g., \Process(*)\*) into individual counter paths
std::vector<std::wstring> ExpandCounterPath(const std::wstring& wildcardPath)
{
    std::vector<std::wstring> result;

    DWORD bufSize = 0;
    PDH_STATUS status = PdhExpandWildCardPathW(
        nullptr, wildcardPath.c_str(), nullptr, &bufSize, 0);

    if (status != PDH_MORE_DATA && status != ERROR_SUCCESS)
    {
        // Not a wildcard or expansion failed — return as-is
        result.push_back(wildcardPath);
        return result;
    }

    std::vector<wchar_t> buffer(bufSize);
    status = PdhExpandWildCardPathW(
        nullptr, wildcardPath.c_str(), buffer.data(), &bufSize, 0);

    if (status != ERROR_SUCCESS)
    {
        result.push_back(wildcardPath);
        return result;
    }

    // Buffer contains null-separated strings terminated by double null
    const wchar_t* p = buffer.data();
    while (*p)
    {
        result.push_back(p);
        p += wcslen(p) + 1;
    }

    return result;
}

} // anonymous namespace

namespace KTDiag
{

PerfController::PerfController() = default;

PerfController::~PerfController()
{
    if (m_active)
        Stop();
}

bool PerfController::Start(const Config& config, int intervalMs,
                            const std::filesystem::path& outputDir)
{
    if (m_active)
    {
        m_error = L"Performance logging is already active";
        return false;
    }

    const auto& configCounters = config.GetPerfCounters();
    if (configCounters.empty())
    {
        m_error = L"No performance counters configured";
        return false;
    }

    // Create output directory
    std::filesystem::path perfDir = outputDir / L"perf_logs";
    std::error_code ec;
    std::filesystem::create_directories(perfDir, ec);
    if (ec)
    {
        m_error = L"Failed to create perf_logs directory";
        return false;
    }

    m_csvPath = perfDir / L"perf_counters.csv";
    m_intervalMs = intervalMs;

    // Open PDH query
    PDH_STATUS pdhStatus = PdhOpenQueryW(nullptr, 0,
        reinterpret_cast<PDH_HQUERY*>(&m_queryHandle));
    if (pdhStatus != ERROR_SUCCESS)
    {
        m_error = L"PdhOpenQuery failed (status " + std::to_wstring(pdhStatus) + L")";
        Logger::Instance().Log(LogLevel::Error, L"[Perf] %s", m_error.c_str());
        return false;
    }

    // Expand wildcard paths and add counters
    m_counterHandles.clear();
    m_counterPaths.clear();

    for (const auto& counterPattern : configCounters)
    {
        auto expanded = ExpandCounterPath(counterPattern);

        for (const auto& counterPath : expanded)
        {
            PDH_HCOUNTER hCounter = nullptr;
            pdhStatus = PdhAddEnglishCounterW(
                reinterpret_cast<PDH_HQUERY>(m_queryHandle),
                counterPath.c_str(), 0, &hCounter);

            if (pdhStatus != ERROR_SUCCESS)
            {
                Logger::Instance().Log(LogLevel::Warning,
                    L"[Perf] Failed to add counter '%s' (status %ld), skipping",
                    counterPath.c_str(), pdhStatus);
                continue;
            }

            m_counterHandles.push_back(hCounter);
            m_counterPaths.push_back(counterPath);
        }
    }

    if (m_counterHandles.empty())
    {
        PdhCloseQuery(reinterpret_cast<PDH_HQUERY>(m_queryHandle));
        m_queryHandle = nullptr;
        m_error = L"No valid performance counters could be added";
        return false;
    }

    Logger::Instance().Log(LogLevel::Info,
        L"[Perf] Started with %zu counters, interval %dms, output: %s",
        m_counterHandles.size(), m_intervalMs, m_csvPath.wstring().c_str());

    // Do an initial collect to prime the counters (first collect returns no data)
    PdhCollectQueryData(reinterpret_cast<PDH_HQUERY>(m_queryHandle));

    // Start collection thread
    m_stopFlag = false;
    m_active = true;
    m_thread = std::thread(&PerfController::CollectThread, this);

    return true;
}

bool PerfController::Stop()
{
    if (!m_active)
    {
        m_error = L"Performance logging is not active";
        return false;
    }

    m_stopFlag = true;
    if (m_thread.joinable())
        m_thread.join();

    // Close PDH query (also closes all counter handles)
    if (m_queryHandle)
    {
        PdhCloseQuery(reinterpret_cast<PDH_HQUERY>(m_queryHandle));
        m_queryHandle = nullptr;
    }
    m_counterHandles.clear();

    m_active = false;
    Logger::Instance().Log(LogLevel::Info, L"[Perf] Stopped, output: %s",
                           m_csvPath.wstring().c_str());
    return true;
}

void PerfController::CollectThread()
{
    std::wofstream csv(m_csvPath);
    if (!csv.is_open())
    {
        Logger::Instance().Log(LogLevel::Error,
            L"[Perf] Failed to create CSV: %s", m_csvPath.wstring().c_str());
        return;
    }

    // Write CSV header
    csv << L"Timestamp,Counter,Value" << std::endl;

    while (!m_stopFlag)
    {
        PDH_STATUS status = PdhCollectQueryData(
            reinterpret_cast<PDH_HQUERY>(m_queryHandle));

        if (status != ERROR_SUCCESS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[Perf] PdhCollectQueryData failed (status %ld)", status);
            std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs));
            continue;
        }

        std::wstring timestamp = GetTimestamp();

        for (size_t i = 0; i < m_counterHandles.size(); ++i)
        {
            PDH_FMT_COUNTERVALUE counterVal = {};
            status = PdhGetFormattedCounterValue(
                reinterpret_cast<PDH_HCOUNTER>(m_counterHandles[i]),
                PDH_FMT_DOUBLE, nullptr, &counterVal);

            if (status == ERROR_SUCCESS &&
                (counterVal.CStatus == PDH_CSTATUS_VALID_DATA ||
                 counterVal.CStatus == PDH_CSTATUS_NEW_DATA))
            {
                // Escape counter path if it contains commas
                const auto& path = m_counterPaths[i];
                if (path.find(L',') != std::wstring::npos)
                    csv << timestamp << L",\"" << path << L"\"," << counterVal.doubleValue << std::endl;
                else
                    csv << timestamp << L"," << path << L"," << counterVal.doubleValue << std::endl;
            }
        }

        // Sleep in small increments to check stop flag
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(m_intervalMs);
        while (!m_stopFlag && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    csv.close();
}

} // namespace KTDiag
