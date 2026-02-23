#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "WppController.h"
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

bool ParseGuid(const std::wstring& guidStr, GUID& outGuid)
{
    std::wstring formatted = guidStr;
    if (!formatted.empty() && formatted[0] != L'{')
        formatted = L"{" + formatted + L"}";

    HRESULT hr = CLSIDFromString(formatted.c_str(), &outGuid);
    return SUCCEEDED(hr);
}

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

ULONGLONG ParseFlagsHex(const std::wstring& flagsStr)
{
    if (flagsStr.empty())
        return 0xFFFF;

    ULONGLONG val = 0;
    // Skip "0x" or "0X" prefix
    const wchar_t* p = flagsStr.c_str();
    if (flagsStr.size() > 2 && (flagsStr[0] == L'0') &&
        (flagsStr[1] == L'x' || flagsStr[1] == L'X'))
    {
        p += 2;
    }

    while (*p)
    {
        val <<= 4;
        if (*p >= L'0' && *p <= L'9')
            val |= (*p - L'0');
        else if (*p >= L'a' && *p <= L'f')
            val |= (*p - L'a' + 10);
        else if (*p >= L'A' && *p <= L'F')
            val |= (*p - L'A' + 10);
        ++p;
    }
    return val;
}

// Map trace level integer (1-5) to TRACE_LEVEL constants
UCHAR MapTraceLevel(int level)
{
    switch (level)
    {
    case 1: return TRACE_LEVEL_CRITICAL;
    case 2: return TRACE_LEVEL_ERROR;
    case 3: return TRACE_LEVEL_WARNING;
    case 4: return TRACE_LEVEL_INFORMATION;
    case 5: return TRACE_LEVEL_VERBOSE;
    default: return TRACE_LEVEL_WARNING;
    }
}

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

// Parse registry path like "HKLM\SOFTWARE\ExampleAV\Logging\Level"
// into root key + subkey path
bool ParseRegistryPath(const std::wstring& fullPath, HKEY& rootKey, std::wstring& subKey, std::wstring& valueName)
{
    // Split into root\subkey\valueName
    size_t firstSlash = fullPath.find(L'\\');
    if (firstSlash == std::wstring::npos)
        return false;

    std::wstring rootStr = fullPath.substr(0, firstSlash);
    std::wstring remainder = fullPath.substr(firstSlash + 1);

    if (rootStr == L"HKLM" || rootStr == L"HKEY_LOCAL_MACHINE")
        rootKey = HKEY_LOCAL_MACHINE;
    else if (rootStr == L"HKCU" || rootStr == L"HKEY_CURRENT_USER")
        rootKey = HKEY_CURRENT_USER;
    else
        return false;

    // Last segment is value name, everything before is subkey
    size_t lastSlash = remainder.rfind(L'\\');
    if (lastSlash == std::wstring::npos)
        return false;

    subKey = remainder.substr(0, lastSlash);
    valueName = remainder.substr(lastSlash + 1);
    return true;
}

} // anonymous namespace

namespace KTDiag
{

WppController::WppController() = default;

WppController::~WppController()
{
    if (m_active)
        Stop();
}

bool WppController::Start(const Config& config, int traceLevel,
                           const std::filesystem::path& outputDir)
{
    if (m_active)
    {
        m_error = L"WPP tracing is already active";
        return false;
    }

    const auto& providers = config.GetWppProviders();
    if (providers.empty())
    {
        m_error = L"No WPP providers configured";
        return false;
    }

    // Create output subdirectory
    std::filesystem::path wppDir = outputDir / L"wpp_traces";
    std::error_code ec;
    std::filesystem::create_directories(wppDir, ec);
    if (ec)
    {
        m_error = L"Failed to create wpp_traces directory";
        return false;
    }

    UCHAR trLevel = MapTraceLevel(traceLevel);
    m_traceLevel = traceLevel;
    m_sessions.clear();
    m_etlPaths.clear();

    Logger::Instance().Log(LogLevel::Info,
        L"[WPP] Starting WPP tracing with %zu providers at level %d",
        providers.size(), traceLevel);

    for (const auto& provider : providers)
    {
        GUID providerGuid;
        if (!ParseGuid(provider.guid, providerGuid))
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[WPP] Failed to parse GUID '%s' for provider '%s', skipping",
                provider.guid.c_str(), provider.name.c_str());
            continue;
        }

        std::wstring safeName = SanitizeProviderName(provider.name);
        std::wstring sessionName = L"KTDiag_WPP_" + safeName;
        std::filesystem::path etlPath = wppDir / (safeName + L".etl");

        TracePropertiesBuffer propBuf(sessionName, etlPath.wstring());

        TRACEHANDLE hTrace = 0;
        ULONG status = StartTraceW(&hTrace, sessionName.c_str(), propBuf.props);

        if (status == ERROR_ALREADY_EXISTS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[WPP] Session '%s' already exists, stopping stale session",
                sessionName.c_str());
            StopSessionByName(sessionName);

            // Retry
            TracePropertiesBuffer propBuf2(sessionName, etlPath.wstring());
            status = StartTraceW(&hTrace, sessionName.c_str(), propBuf2.props);
        }

        if (status != ERROR_SUCCESS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[WPP] StartTraceW failed for '%s' (error %lu)",
                sessionName.c_str(), status);
            continue;
        }

        // Enable the provider with configured level and flags
        ULONGLONG matchAnyKeyword = ParseFlagsHex(provider.flags);

        status = EnableTraceEx2(
            hTrace,
            &providerGuid,
            EVENT_CONTROL_CODE_ENABLE_PROVIDER,
            trLevel,
            matchAnyKeyword,
            0,       // MatchAllKeyword
            0,       // Timeout
            nullptr  // EnableParameters
        );

        if (status != ERROR_SUCCESS)
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[WPP] EnableTraceEx2 failed for '%s' (error %lu)",
                provider.name.c_str(), status);
            // Stop the session we just started
            StopSessionByName(sessionName);
            continue;
        }

        SessionInfo si;
        si.handle = hTrace;
        si.sessionName = sessionName;
        si.etlPath = etlPath;
        m_sessions.push_back(std::move(si));

        Logger::Instance().Log(LogLevel::Info,
            L"[WPP] Started session '%s' -> %s",
            sessionName.c_str(), etlPath.wstring().c_str());
    }

    if (m_sessions.empty())
    {
        m_error = L"Failed to start any WPP trace sessions";
        return false;
    }

    m_active = true;
    return true;
}

bool WppController::Stop()
{
    if (!m_active)
    {
        m_error = L"WPP tracing is not active";
        return false;
    }

    m_etlPaths.clear();

    for (auto& session : m_sessions)
    {
        ULONG nameSize = static_cast<ULONG>((session.sessionName.size() + 1) * sizeof(wchar_t));
        ULONG totalSize = sizeof(EVENT_TRACE_PROPERTIES) + nameSize + sizeof(wchar_t);
        std::vector<BYTE> buf(totalSize, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
        props->Wnode.BufferSize = totalSize;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        ULONG status = ControlTraceW(session.handle, nullptr, props, EVENT_TRACE_CONTROL_STOP);

        if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA)
        {
            Logger::Instance().Log(LogLevel::Info,
                L"[WPP] Stopped session '%s'", session.sessionName.c_str());
            m_etlPaths.push_back(session.etlPath);
        }
        else
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[WPP] ControlTraceW stop failed for '%s' (error %lu)",
                session.sessionName.c_str(), status);
        }
    }

    m_sessions.clear();
    m_active = false;
    return true;
}

bool WppController::SetProductLogLevel(const Config& config, const std::wstring& level)
{
    const auto& logLevels = config.GetProductLogLevels();
    if (logLevels.levelRegistryKey.empty())
        return false;

    HKEY rootKey;
    std::wstring subKey, valueName;
    if (!ParseRegistryPath(logLevels.levelRegistryKey, rootKey, subKey, valueName))
        return false;

    HKEY hKey;
    LONG result = RegCreateKeyExW(rootKey, subKey.c_str(), 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                                   &hKey, nullptr);
    if (result != ERROR_SUCCESS)
        return false;

    result = RegSetValueExW(hKey, valueName.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(level.c_str()),
                            static_cast<DWORD>((level.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    if (result == ERROR_SUCCESS)
    {
        Logger::Instance().Log(LogLevel::Info,
            L"[WPP] Set product log level to '%s' at '%s'",
            level.c_str(), logLevels.levelRegistryKey.c_str());
    }
    return result == ERROR_SUCCESS;
}

std::wstring WppController::GetProductLogLevel(const Config& config)
{
    const auto& logLevels = config.GetProductLogLevels();
    if (logLevels.levelRegistryKey.empty())
        return L"";

    HKEY rootKey;
    std::wstring subKey, valueName;
    if (!ParseRegistryPath(logLevels.levelRegistryKey, rootKey, subKey, valueName))
        return L"";

    HKEY hKey;
    LONG result = RegOpenKeyExW(rootKey, subKey.c_str(), 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
        return logLevels.defaultLevel;

    wchar_t buffer[256] = {};
    DWORD bufSize = sizeof(buffer);
    DWORD type = 0;
    result = RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type,
                              reinterpret_cast<BYTE*>(buffer), &bufSize);
    RegCloseKey(hKey);

    if (result == ERROR_SUCCESS && type == REG_SZ)
        return std::wstring(buffer);

    return logLevels.defaultLevel;
}

} // namespace KTDiag
