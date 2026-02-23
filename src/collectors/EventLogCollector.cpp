#include "EventLogCollector.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <winevt.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

#pragma comment(lib, "wevtapi.lib")

namespace
{

// Number of days back to filter events
constexpr int DAYS_BACK = 7;

// Number of events to fetch per EvtNext call
constexpr DWORD BATCH_SIZE = 100;

std::wstring SanitizeChannelName(const std::wstring& channel)
{
    std::wstring result = channel;
    const std::wstring invalidChars = L"/\\:*?\"<>|";
    for (auto& ch : result)
    {
        if (invalidChars.find(ch) != std::wstring::npos)
            ch = L'_';
    }
    return result;
}

std::wstring BuildTimeFilter(int daysBack)
{
    // timediff uses milliseconds
    long long ms = static_cast<long long>(daysBack) * 24LL * 60LL * 60LL * 1000LL;
    std::wostringstream oss;
    oss << L"*[System[TimeCreated[timediff(@SystemTime) <= " << ms << L"]]]";
    return oss.str();
}

bool ExportChannelEvtx(const std::wstring& channel,
                       const std::filesystem::path& outputFile,
                       int daysBack,
                       std::wstring& errorOut)
{
    std::wstring query = BuildTimeFilter(daysBack);

    BOOL ok = EvtExportLog(
        nullptr,                    // session (local)
        channel.c_str(),            // channel path
        query.c_str(),              // query / filter
        outputFile.wstring().c_str(),
        EvtExportLogChannelPath     // flags
    );

    if (!ok)
    {
        DWORD err = GetLastError();
        if (err == 15007) // ERROR_EVT_CHANNEL_NOT_FOUND
        {
            KTDiag::Logger::Instance().Log(KTDiag::LogLevel::Warning,
                L"[EventLog] Channel not found: %s (skipping)", channel.c_str());
            return true; // Not a fatal error
        }
        if (err == 15004) // ERROR_EVT_QUERY_RESULT_STALE or empty
        {
            KTDiag::Logger::Instance().Log(KTDiag::LogLevel::Warning,
                L"[EventLog] No events in channel: %s", channel.c_str());
            return true;
        }
        errorOut = L"EvtExportLog failed for " + channel + L" (error " + std::to_wstring(err) + L")";
        KTDiag::Logger::Instance().Log(KTDiag::LogLevel::Error,
            L"[EventLog] %s", errorOut.c_str());
        return false;
    }
    return true;
}

bool ExportChannelText(const std::wstring& channel,
                       const std::filesystem::path& outputFile,
                       int daysBack,
                       KTDiag::CancelToken cancel,
                       std::wstring& errorOut)
{
    std::wstring query = BuildTimeFilter(daysBack);

    EVT_HANDLE hQuery = EvtQuery(
        nullptr,            // session (local)
        channel.c_str(),    // channel path
        query.c_str(),      // query
        EvtQueryChannelPath | EvtQueryReverseDirection
    );

    if (!hQuery)
    {
        DWORD err = GetLastError();
        if (err == 15007) // ERROR_EVT_CHANNEL_NOT_FOUND
        {
            KTDiag::Logger::Instance().Log(KTDiag::LogLevel::Warning,
                L"[EventLog] Channel not found for text export: %s (skipping)", channel.c_str());
            return true;
        }
        errorOut = L"EvtQuery failed for " + channel + L" (error " + std::to_wstring(err) + L")";
        return false;
    }

    std::wofstream file(outputFile);
    if (!file.is_open())
    {
        EvtClose(hQuery);
        errorOut = L"Failed to create text file: " + outputFile.wstring();
        return false;
    }

    file << L"=== Event Log: " << channel << L" ===" << std::endl;
    file << L"Filter: Last " << daysBack << L" days" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    std::vector<EVT_HANDLE> events(BATCH_SIZE);
    DWORD returned = 0;
    DWORD totalEvents = 0;

    while (true)
    {
        if (cancel && cancel->load())
        {
            EvtClose(hQuery);
            return false;
        }

        if (!EvtNext(hQuery, BATCH_SIZE, events.data(), INFINITE, 0, &returned))
        {
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS)
                break;
            // Non-fatal: log and stop iterating
            KTDiag::Logger::Instance().Log(KTDiag::LogLevel::Warning,
                L"[EventLog] EvtNext error %lu for channel %s", err, channel.c_str());
            break;
        }

        for (DWORD i = 0; i < returned; ++i)
        {
            // Render event as XML
            DWORD bufferUsed = 0;
            DWORD propertyCount = 0;

            // First call to get required buffer size
            EvtRender(nullptr, events[i], EvtRenderEventXml, 0, nullptr, &bufferUsed, &propertyCount);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && bufferUsed > 0)
            {
                std::vector<wchar_t> buffer(bufferUsed / sizeof(wchar_t) + 1);
                if (EvtRender(nullptr, events[i], EvtRenderEventXml,
                              static_cast<DWORD>(buffer.size() * sizeof(wchar_t)),
                              buffer.data(), &bufferUsed, &propertyCount))
                {
                    file << buffer.data() << std::endl << std::endl;
                }
            }

            EvtClose(events[i]);
            events[i] = nullptr;
            ++totalEvents;
        }
    }

    file << std::endl << L"=== Total events rendered: " << totalEvents << L" ===" << std::endl;
    file.close();
    EvtClose(hQuery);

    KTDiag::Logger::Instance().Log(KTDiag::LogLevel::Info,
        L"[EventLog] Exported %lu events from %s as text", totalEvents, channel.c_str());
    return true;
}

} // anonymous namespace

namespace KTDiag
{

bool EventLogCollector::Collect(const Config& config,
                                 const std::filesystem::path& outputDir,
                                 ProgressCallback cb,
                                 CancelToken cancel)
{
    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    const auto& channels = config.GetEventLogChannels();
    if (channels.empty())
    {
        if (cb) cb(Name(), L"No event log channels configured", 100);
        Logger::Instance().Log(LogLevel::Info, L"[EventLog] No channels configured, skipping");
        return true;
    }

    // Create output subdirectory
    std::filesystem::path evtDir = outputDir / L"eventlogs";
    std::error_code ec;
    std::filesystem::create_directories(evtDir, ec);
    if (ec)
    {
        m_error = L"Failed to create eventlogs directory";
        return false;
    }

    if (cb) cb(Name(), L"Exporting event logs...", 0);

    int completed = 0;
    int total = static_cast<int>(channels.size());
    bool anyFailure = false;

    for (const auto& channel : channels)
    {
        if (cancel && cancel->load())
        {
            m_error = L"Cancelled";
            return false;
        }

        std::wstring safeName = SanitizeChannelName(channel);
        int pct = (completed * 100) / total;

        if (cb) cb(Name(), L"Exporting: " + channel, pct);

        // Export .evtx binary
        std::wstring evtxError;
        std::filesystem::path evtxFile = evtDir / (safeName + L".evtx");
        if (!ExportChannelEvtx(channel, evtxFile, DAYS_BACK, evtxError))
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[EventLog] EVTX export failed for %s: %s", channel.c_str(), evtxError.c_str());
            anyFailure = true;
        }

        // Export .txt human-readable
        std::wstring txtError;
        std::filesystem::path txtFile = evtDir / (safeName + L".txt");
        if (!ExportChannelText(channel, txtFile, DAYS_BACK, cancel, txtError))
        {
            if (cancel && cancel->load())
            {
                m_error = L"Cancelled";
                return false;
            }
            Logger::Instance().Log(LogLevel::Warning,
                L"[EventLog] Text export failed for %s: %s", channel.c_str(), txtError.c_str());
            anyFailure = true;
        }

        ++completed;
    }

    if (cb) cb(Name(), L"Done", 100);

    if (anyFailure)
    {
        Logger::Instance().Log(LogLevel::Warning,
            L"[EventLog] Completed with some channel failures (%d/%d channels)", completed, total);
    }
    else
    {
        Logger::Instance().Log(LogLevel::Info,
            L"[EventLog] Successfully exported %d channels", total);
    }

    return true;
}

} // namespace KTDiag
