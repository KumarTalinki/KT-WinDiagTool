#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "EtwController.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <objbase.h>
#include <evntrace.h>
#include <evntcons.h>
#include <vector>
#include <string>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{

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
    if (result.size() > 200)
        result.resize(200);
    return result;
}

bool ParseGuid(const std::wstring& guidStr, GUID& outGuid)
{
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

        memcpy(buffer.data() + props->LogFileNameOffset,
               logFilePath.c_str(), logFileSize);
    }
};

void StopSessionByName(const std::wstring& sessionName)
{
    ULONG nameSize = static_cast<ULONG>((sessionName.size() + 1) * sizeof(wchar_t));
    ULONG totalSize = sizeof(EVENT_TRACE_PROPERTIES) + nameSize + sizeof(wchar_t);
    std::vector<BYTE> buf(totalSize, 0);
    auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
    p->Wnode.BufferSize = totalSize;
    p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    ControlTraceW(0, sessionName.c_str(), p, EVENT_TRACE_CONTROL_STOP);
}

} // anonymous namespace

namespace KTDiag
{

EtwController::EtwController() = default;

EtwController::~EtwController()
{
    if (m_active)
        Stop();
}

bool EtwController::Start(const Config& config, const std::filesystem::path& outputDir)
{
    if (m_active)
    {
        m_error = L"ETW controller is already active";
        return false;
    }

    m_sessions.clear();
    m_etlPaths.clear();
    m_error.clear();

    // Create output directory
    std::filesystem::path etwDir = outputDir / L"etw";
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

    for (const auto& ep : config.GetEtwProviders())
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
                L"[EtwCtrl] Failed to parse GUID '%s' for provider '%s', skipping",
                ep.guid.c_str(), ep.name.c_str());
        }
    }

    // --- Combined session (kernel I/O + all product providers) ---
    {
        std::wstring sessionName = L"KTDiag_ETW";
        std::filesystem::path combinedFile = etwDir / L"KTDiag_combined.etl";

        TracePropertiesBuffer propBuf(sessionName, combinedFile.wstring());

        TRACEHANDLE hTrace = 0;
        ULONG status = StartTraceW(&hTrace, sessionName.c_str(), propBuf.props);

        if (status == ERROR_ALREADY_EXISTS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[EtwCtrl] Session '%s' already exists, stopping stale session",
                sessionName.c_str());
            StopSessionByName(sessionName);

            TracePropertiesBuffer propBuf2(sessionName, combinedFile.wstring());
            status = StartTraceW(&hTrace, sessionName.c_str(), propBuf2.props);
        }

        if (status != ERROR_SUCCESS)
        {
            m_error = L"Failed to start combined ETW session (error " +
                       std::to_wstring(status) + L")";
            Logger::Instance().Log(LogLevel::Error, L"[EtwCtrl] %s", m_error.c_str());
            return false;
        }

        // Enable built-in providers
        ULONG enableStatus = EnableTraceEx2(hTrace, &GUID_KERNEL_FILE,
            EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
            0, 0, 0, nullptr);
        if (enableStatus != ERROR_SUCCESS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[EtwCtrl] Failed to enable Kernel-File provider (error %u)", enableStatus);
        }

        enableStatus = EnableTraceEx2(hTrace, &GUID_KERNEL_DISK,
            EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
            0, 0, 0, nullptr);
        if (enableStatus != ERROR_SUCCESS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[EtwCtrl] Failed to enable Kernel-Disk provider (error %u)", enableStatus);
        }

        // Enable product-specific providers
        for (const auto& pp : productProviders)
        {
            enableStatus = EnableTraceEx2(hTrace, &pp.guid,
                EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
                0, 0, 0, nullptr);
            if (enableStatus != ERROR_SUCCESS)
            {
                Logger::Instance().Log(LogLevel::Warning,
                    L"[EtwCtrl] Failed to enable provider '%s' (error %u)",
                    pp.name.c_str(), enableStatus);
            }
        }

        SessionInfo si;
        si.handle = static_cast<unsigned __int64>(hTrace);
        si.sessionName = sessionName;
        si.etlPath = combinedFile;
        m_sessions.push_back(si);

        Logger::Instance().Log(LogLevel::Info,
            L"[EtwCtrl] Combined ETW session started: %s", combinedFile.wstring().c_str());
    }

    // --- Per-provider sessions ---
    for (const auto& pp : productProviders)
    {
        std::wstring safeName = SanitizeProviderName(pp.name);
        std::wstring sessionName = L"KTDiag_" + safeName;
        std::filesystem::path providerFile = etwDir / (safeName + L".etl");

        TracePropertiesBuffer propBuf(sessionName, providerFile.wstring());

        TRACEHANDLE hTrace = 0;
        ULONG status = StartTraceW(&hTrace, sessionName.c_str(), propBuf.props);

        if (status == ERROR_ALREADY_EXISTS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[EtwCtrl] Session '%s' already exists, stopping stale session",
                sessionName.c_str());
            StopSessionByName(sessionName);

            TracePropertiesBuffer propBuf2(sessionName, providerFile.wstring());
            status = StartTraceW(&hTrace, sessionName.c_str(), propBuf2.props);
        }

        if (status != ERROR_SUCCESS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[EtwCtrl] Failed to start session '%s' (error %u)",
                sessionName.c_str(), status);
            continue;
        }

        ULONG enableStatus = EnableTraceEx2(hTrace, &pp.guid,
            EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
            0, 0, 0, nullptr);
        if (enableStatus != ERROR_SUCCESS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[EtwCtrl] Failed to enable provider '%s' in dedicated session (error %u)",
                pp.name.c_str(), enableStatus);
        }

        SessionInfo si;
        si.handle = static_cast<unsigned __int64>(hTrace);
        si.sessionName = sessionName;
        si.etlPath = providerFile;
        m_sessions.push_back(si);

        Logger::Instance().Log(LogLevel::Info,
            L"[EtwCtrl] Per-provider session started for '%s': %s",
            pp.name.c_str(), providerFile.wstring().c_str());
    }

    m_active = !m_sessions.empty();
    if (!m_active)
    {
        m_error = L"No ETW sessions could be started";
        return false;
    }

    Logger::Instance().Log(LogLevel::Info,
        L"[EtwCtrl] Started %zu ETW session(s)", m_sessions.size());
    return true;
}

bool EtwController::Stop()
{
    if (!m_active)
        return false;

    m_etlPaths.clear();

    for (auto& si : m_sessions)
    {
        ULONG nameSize = static_cast<ULONG>((si.sessionName.size() + 1) * sizeof(wchar_t));
        ULONG totalSize = sizeof(EVENT_TRACE_PROPERTIES) + nameSize + sizeof(wchar_t);
        std::vector<BYTE> buf(totalSize, 0);
        auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
        p->Wnode.BufferSize = totalSize;
        p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        TRACEHANDLE h = static_cast<TRACEHANDLE>(si.handle);
        ULONG status = ControlTraceW(h, nullptr, p, EVENT_TRACE_CONTROL_STOP);
        if (status != ERROR_SUCCESS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[EtwCtrl] Failed to stop session '%s' (error %u)",
                si.sessionName.c_str(), status);
        }
        else
        {
            Logger::Instance().Log(LogLevel::Info,
                L"[EtwCtrl] Stopped session '%s'", si.sessionName.c_str());
        }

        if (std::filesystem::exists(si.etlPath))
            m_etlPaths.push_back(si.etlPath);
    }

    m_sessions.clear();
    m_active = false;
    return true;
}

} // namespace KTDiag
