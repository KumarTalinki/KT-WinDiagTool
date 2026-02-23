#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "EtwLogCollector.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <objbase.h>
#include <evntrace.h>
#include <evntcons.h>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{

// Fallback used only if config returns 0 or negative
constexpr int DEFAULT_DURATION_SECONDS = 30;

// Built-in File I/O providers (always captured)
// Microsoft-Windows-Kernel-File
static const GUID GUID_KERNEL_FILE = { 0xEDD08927, 0x9CC4, 0x4E65,
    { 0xB9, 0x70, 0xC2, 0x56, 0x0F, 0xB5, 0xC2, 0x89 } };

// Microsoft-Windows-Kernel-Disk
static const GUID GUID_KERNEL_DISK = { 0xC7BDE69A, 0xE1E0, 0x4177,
    { 0xB6, 0xEF, 0x28, 0x3A, 0xD1, 0x52, 0x52, 0x71 } };

std::wstring SanitizeProviderName(const std::wstring& name)
{
    std::wstring result = name;
    const std::wstring invalidChars = L"/\\:*?\"<>| ";
    for (auto& ch : result)
    {
        if (invalidChars.find(ch) != std::wstring::npos)
            ch = L'_';
    }
    // Truncate to 200 chars for session naming
    if (result.size() > 200)
        result.resize(200);
    return result;
}

bool ParseGuid(const std::wstring& guidStr, GUID& outGuid)
{
    // CLSIDFromString expects {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
    std::wstring formatted = guidStr;
    if (!formatted.empty() && formatted[0] != L'{')
        formatted = L"{" + formatted + L"}";

    HRESULT hr = CLSIDFromString(formatted.c_str(), &outGuid);
    return SUCCEEDED(hr);
}

// Allocate EVENT_TRACE_PROPERTIES with space for session name and log file path
struct TracePropertiesBuffer
{
    std::vector<BYTE> buffer;
    EVENT_TRACE_PROPERTIES* props;

    TracePropertiesBuffer(const std::wstring& sessionName,
                          const std::wstring& logFilePath)
    {
        ULONG sessionNameSize = static_cast<ULONG>((sessionName.size() + 1) * sizeof(wchar_t));
        ULONG logFileSize = static_cast<ULONG>((logFilePath.size() + 1) * sizeof(wchar_t));
        ULONG totalSize = sizeof(EVENT_TRACE_PROPERTIES) + sessionNameSize + logFileSize;

        buffer.resize(totalSize, 0);
        props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());

        props->Wnode.BufferSize = totalSize;
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->Wnode.ClientContext = 1; // QPC clock
        props->LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;
        props->MaximumFileSize = 256; // MB
        props->BufferSize = 64;       // KB per buffer
        props->MinimumBuffers = 8;
        props->MaximumBuffers = 64;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        props->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + sessionNameSize;

        // Copy log file path
        memcpy(buffer.data() + props->LogFileNameOffset,
               logFilePath.c_str(), logFileSize);
    }
};

// RAII guard to ensure trace session is stopped on scope exit
struct TraceSessionGuard
{
    TRACEHANDLE handle;
    std::wstring sessionName;
    bool active;

    TraceSessionGuard() : handle(0), active(false) {}

    ~TraceSessionGuard()
    {
        Stop();
    }

    void Stop()
    {
        if (active && handle != 0)
        {
            // Allocate properties for ControlTrace stop
            ULONG nameSize = static_cast<ULONG>((sessionName.size() + 1) * sizeof(wchar_t));
            ULONG totalSize = sizeof(EVENT_TRACE_PROPERTIES) + nameSize + sizeof(wchar_t);
            std::vector<BYTE> buf(totalSize, 0);
            auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
            p->Wnode.BufferSize = totalSize;
            p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

            ControlTraceW(handle, nullptr, p, EVENT_TRACE_CONTROL_STOP);
            active = false;
            handle = 0;
        }
    }

    // Non-copyable
    TraceSessionGuard(const TraceSessionGuard&) = delete;
    TraceSessionGuard& operator=(const TraceSessionGuard&) = delete;
};

bool StartSession(const std::wstring& sessionName,
                  const std::wstring& logFilePath,
                  TraceSessionGuard& guard,
                  std::wstring& errorOut)
{
    TracePropertiesBuffer propBuf(sessionName, logFilePath);

    TRACEHANDLE hTrace = 0;
    ULONG status = StartTraceW(&hTrace, sessionName.c_str(), propBuf.props);

    if (status == ERROR_ALREADY_EXISTS)
    {
        // Stop stale session and retry
        KTDiag::Logger::Instance().Log(KTDiag::LogLevel::Warning,
            L"[ETW] Session '%s' already exists, stopping stale session", sessionName.c_str());

        ULONG nameSize = static_cast<ULONG>((sessionName.size() + 1) * sizeof(wchar_t));
        ULONG stopSize = sizeof(EVENT_TRACE_PROPERTIES) + nameSize + sizeof(wchar_t);
        std::vector<BYTE> stopBuf(stopSize, 0);
        auto* stopProps = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(stopBuf.data());
        stopProps->Wnode.BufferSize = stopSize;
        stopProps->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        ControlTraceW(0, sessionName.c_str(), stopProps, EVENT_TRACE_CONTROL_STOP);

        // Retry start
        TracePropertiesBuffer propBuf2(sessionName, logFilePath);
        status = StartTraceW(&hTrace, sessionName.c_str(), propBuf2.props);
    }

    if (status != ERROR_SUCCESS)
    {
        errorOut = L"StartTraceW failed for session '" + sessionName + L"' (error " +
                   std::to_wstring(status) + L")";
        return false;
    }

    guard.handle = hTrace;
    guard.sessionName = sessionName;
    guard.active = true;
    return true;
}

bool EnableProvider(TRACEHANDLE hTrace, const GUID& providerGuid,
                    const std::wstring& providerName, std::wstring& errorOut)
{
    ULONG status = EnableTraceEx2(
        hTrace,
        &providerGuid,
        EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_VERBOSE,
        0,  // MatchAnyKeyword (all)
        0,  // MatchAllKeyword
        0,  // Timeout
        nullptr  // EnableParameters
    );

    if (status != ERROR_SUCCESS)
    {
        errorOut = L"EnableTraceEx2 failed for " + providerName +
                   L" (error " + std::to_wstring(status) + L")";
        return false;
    }
    return true;
}

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
        if (cb) cb(name, L"Capturing ETW traces... (" +
                   std::to_wstring(durationSeconds - i) + L"s remaining)", pct);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // anonymous namespace

namespace KTDiag
{

bool EtwLogCollector::Collect(const Config& config,
                               const std::filesystem::path& outputDir,
                               ProgressCallback cb,
                               CancelToken cancel)
{
    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    // Check if ETW files already exist (e.g., from repro mode)
    {
        if (std::filesystem::exists(outputDir))
        {
            std::error_code scanEc;
            for (const auto& entry : std::filesystem::directory_iterator(outputDir, scanEc))
            {
                if (entry.path().extension() == L".etl")
                {
                    if (cb) cb(Name(), L"ETW traces already captured (repro mode), skipping", 100);
                    Logger::Instance().Log(LogLevel::Info,
                        L"[ETW] ETW traces already exist in output dir, skipping capture");
                    return true;
                }
            }
        }
    }

    const auto& configProviders = config.GetEtwProviders();

    if (cb) cb(Name(), L"Setting up ETW trace sessions...", 0);

    // Use outputDir directly — CollectorEngine already created <base>/etw/ for us
    std::filesystem::path etwDir = outputDir;
    std::error_code ec;
    std::filesystem::create_directories(etwDir, ec);
    if (ec)
    {
        m_error = L"Failed to create etw directory";
        return false;
    }

    // Parse product-specific provider GUIDs
    struct ParsedProvider
    {
        GUID guid;
        std::wstring name;
    };
    std::vector<ParsedProvider> productProviders;

    for (const auto& ep : configProviders)
    {
        ParsedProvider pp;
        if (ParseGuid(ep.guid, pp.guid))
        {
            pp.name = ep.name;
            productProviders.push_back(pp);
        }
        else
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[ETW] Failed to parse GUID '%s' for provider '%s', skipping",
                ep.guid.c_str(), ep.name.c_str());
        }
    }

    bool anyFailure = false;

    int captureSecs = config.GetCaptureDurationSeconds();
    if (captureSecs <= 0) captureSecs = DEFAULT_DURATION_SECONDS;

    // --- Step A: Combined session (File I/O + all providers) ---
    {
        std::wstring sessionName = L"KTDiag_ETW";
        std::filesystem::path combinedFile = etwDir / L"KTDiag_combined.etl";

        TraceSessionGuard guard;
        std::wstring startError;

        if (cb) cb(Name(), L"Starting combined ETW session...", 5);

        if (!StartSession(sessionName, combinedFile.wstring(), guard, startError))
        {
            Logger::Instance().Log(LogLevel::Error, L"[ETW] %s", startError.c_str());
            m_error = startError;
            return false;
        }

        // Enable built-in File I/O providers
        std::wstring enableError;
        if (!EnableProvider(guard.handle, GUID_KERNEL_FILE, L"Microsoft-Windows-Kernel-File", enableError))
        {
            Logger::Instance().Log(LogLevel::Warning, L"[ETW] %s", enableError.c_str());
            anyFailure = true;
        }
        if (!EnableProvider(guard.handle, GUID_KERNEL_DISK, L"Microsoft-Windows-Kernel-Disk", enableError))
        {
            Logger::Instance().Log(LogLevel::Warning, L"[ETW] %s", enableError.c_str());
            anyFailure = true;
        }

        // Enable product-specific providers
        for (const auto& pp : productProviders)
        {
            if (!EnableProvider(guard.handle, pp.guid, pp.name, enableError))
            {
                Logger::Instance().Log(LogLevel::Warning, L"[ETW] %s", enableError.c_str());
                anyFailure = true;
            }
        }

        if (cb) cb(Name(), L"Capturing combined ETW trace...", 10);

        // Wait for capture duration, checking cancel
        WaitWithCancelCheck(captureSecs, cancel, cb, Name(), 10, 60);

        // Stop session (guard destructor will also stop, but explicit is clearer)
        guard.Stop();

        if (cancel && cancel->load())
        {
            m_error = L"Cancelled";
            Logger::Instance().Log(LogLevel::Info, L"[ETW] Combined session cancelled, trace saved");
            return false;
        }

        Logger::Instance().Log(LogLevel::Info,
            L"[ETW] Combined session completed: %s", combinedFile.wstring().c_str());
    }

    // --- Step B: Per-provider sessions (product providers only) ---
    if (!productProviders.empty())
    {
        int providerIdx = 0;
        int totalProviders = static_cast<int>(productProviders.size());

        for (const auto& pp : productProviders)
        {
            if (cancel && cancel->load())
            {
                m_error = L"Cancelled";
                return false;
            }

            std::wstring safeName = SanitizeProviderName(pp.name);
            std::wstring sessionName = L"KTDiag_" + safeName;
            std::filesystem::path providerFile = etwDir / (safeName + L".etl");

            int basePct = 60 + ((providerIdx * 35) / totalProviders);
            int maxPct = 60 + (((providerIdx + 1) * 35) / totalProviders);

            if (cb) cb(Name(), L"Capturing: " + pp.name, basePct);

            TraceSessionGuard guard;
            std::wstring startError;

            if (!StartSession(sessionName, providerFile.wstring(), guard, startError))
            {
                Logger::Instance().Log(LogLevel::Warning, L"[ETW] %s", startError.c_str());
                anyFailure = true;
                ++providerIdx;
                continue;
            }

            std::wstring enableError;
            if (!EnableProvider(guard.handle, pp.guid, pp.name, enableError))
            {
                Logger::Instance().Log(LogLevel::Warning, L"[ETW] %s", enableError.c_str());
                guard.Stop();
                anyFailure = true;
                ++providerIdx;
                continue;
            }

            WaitWithCancelCheck(captureSecs, cancel, cb, Name(), basePct, maxPct);

            guard.Stop();

            if (cancel && cancel->load())
            {
                m_error = L"Cancelled";
                return false;
            }

            Logger::Instance().Log(LogLevel::Info,
                L"[ETW] Per-provider session completed: %s", providerFile.wstring().c_str());
            ++providerIdx;
        }
    }

    if (cb) cb(Name(), L"Done", 100);

    if (anyFailure)
    {
        Logger::Instance().Log(LogLevel::Warning, L"[ETW] Completed with some provider failures");
    }
    else
    {
        Logger::Instance().Log(LogLevel::Info, L"[ETW] All ETW traces captured successfully");
    }

    return true;
}

} // namespace KTDiag
