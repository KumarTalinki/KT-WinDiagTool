#include "Logger.h"

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <shlobj.h>

namespace KTDiag
{

Logger::Logger() = default;

Logger::~Logger()
{
    if (m_file.is_open())
        m_file.close();
}

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

void Logger::Initialize(const std::wstring& baseName)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized)
        return;

    // Create log file in %TEMP%
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);

    // Generate timestamped, PID-qualified filename so each tool invocation
    // gets a unique log — safe for rapid or concurrent programmatic launches.
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t fileName[MAX_PATH] = {};
    swprintf_s(fileName, L"%s%s_%04d%02d%02d_%02d%02d%02d_%lu.log",
               tempPath, baseName.c_str(),
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond,
               GetCurrentProcessId());

    m_logFilePath = fileName;
    m_file.open(m_logFilePath, std::ios::out | std::ios::app);
    m_initialized = true;

    Log(LogLevel::Info, L"Logger initialized: %s", m_logFilePath.c_str());
}

void Logger::Log(LogLevel level, const wchar_t* format, ...)
{
    if (level < m_minLevel)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // Format timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timestamp[64] = {};
    swprintf_s(timestamp, L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    // Format message (heap-allocated to avoid large stack frame)
    constexpr size_t kMsgLen = 4096;
    constexpr size_t kLineLen = kMsgLen + 128;
    auto message = std::make_unique<wchar_t[]>(kMsgLen);
    va_list args;
    va_start(args, format);
    vswprintf_s(message.get(), kMsgLen, format, args);
    va_end(args);

    // Build full line
    auto line = std::make_unique<wchar_t[]>(kLineLen);
    swprintf_s(line.get(), kLineLen, L"[%s] [%s] %s\n",
               timestamp, LevelToString(level), message.get());

    // Write to file
    if (m_file.is_open())
    {
        m_file << line.get();
        m_file.flush();
    }

    // Also write to debug output (visible in Visual Studio)
    OutputDebugStringW(line.get());
}

const wchar_t* Logger::LevelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:   return L"DEBUG";
    case LogLevel::Info:    return L"INFO ";
    case LogLevel::Warning: return L"WARN ";
    case LogLevel::Error:   return L"ERROR";
    default:                return L"?????";
    }
}

} // namespace KTDiag
