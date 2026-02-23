#pragma once

#include <Windows.h>
#include <string>
#include <mutex>
#include <fstream>

namespace KTDiag
{

enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error
};

class Logger
{
public:
    static Logger& Instance();

    // Initialize logging with a base name (creates log file in %TEMP%)
    void Initialize(const std::wstring& baseName);

    // Log a formatted message
    void Log(LogLevel level, const wchar_t* format, ...);

    // Get the path to the current log file
    const std::wstring& GetLogFilePath() const { return m_logFilePath; }

    // Set minimum log level for output
    void SetMinLevel(LogLevel level) { m_minLevel = level; }

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static const wchar_t* LevelToString(LogLevel level);

    std::recursive_mutex m_mutex;
    std::wofstream m_file;
    std::wstring m_logFilePath;
    LogLevel m_minLevel = LogLevel::Debug;
    bool m_initialized = false;
};

} // namespace KTDiag
