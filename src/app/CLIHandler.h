#pragma once

#include "Config.h"

#include <string>
#include <vector>
#include <set>

namespace KTDiag
{

struct CLIOptions
{
    std::wstring configPath;
    std::wstring outputPath;
    std::set<std::wstring> collectors;  // Empty = all
    int duration = 0;                   // 0 = use config default; --duration overrides all timed collectors
    int wppLevel = 3;                   // 1=Critical..5=Verbose, default=Warning
    std::wstring productLogLevel;       // Product log verbosity
    bool enableWpp = false;
    bool disableWpp = false;
    bool enablePerf = false;
    bool disablePerf = false;
    bool reproMode = false;
    bool analyzeDumps = false;
    bool quiet = false;
    bool showHelp = false;
    bool showVersion = false;
    bool collectAll = false;
    int  timeout = 0;                   // 0 = no limit; seconds for overall collection wall time
    std::wstring stopEvent;             // Named Win32 event for programmatic repro stop
};

class CLIHandler
{
public:
    CLIHandler();
    ~CLIHandler();

    // Parse args and execute; returns exit code (0=success, 1=partial, 2=critical)
    int Run(int argc, wchar_t* argv[]);

private:
    bool ParseArgs(int argc, wchar_t* argv[]);
    void PrintHelp();
    void PrintVersion();
    int Execute();

    CLIOptions m_options;
};

} // namespace KTDiag
