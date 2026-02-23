#include "CLIHandler.h"
#include "../util/Logger.h"
#include "../collectors/CollectorEngine.h"
#include "../collectors/EnvVarCollector.h"
#include "../collectors/RegistryCollector.h"
#include "../collectors/InstalledProductsCollector.h"
#include "../collectors/ProductLogCollector.h"
#include "../collectors/InstallLogCollector.h"
#include "../collectors/ServicesCollector.h"
#include "../collectors/ProxySettingsCollector.h"
#include "../collectors/NetworkConfigCollector.h"
#include "../collectors/FirewallConfigCollector.h"
#include "../collectors/UserInfoCollector.h"
#include "../collectors/DiskSpaceCollector.h"
#include "../collectors/WerCollector.h"
#include "../collectors/SystemInfoCollector.h"
#include "../collectors/EventLogCollector.h"
#include "../collectors/EtwLogCollector.h"
#include "../collectors/WppLogCollector.h"
#include "../collectors/PerfLogCollector.h"
#include "../collectors/PacketCaptureCollector.h"
#include "../collectors/CrashDumpCollector.h"
#include "../analysis/DumpAnalyzer.h"
#include "../analysis/WinDbgAutomation.h"
#include "../output/ReportGenerator.h"
#include "../output/ZipPackager.h"
#include "../controllers/WppController.h"
#include "../controllers/PerfController.h"
#include "../controllers/ReproOrchestrator.h"

#include <ShlObj.h>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <future>
#include <chrono>

namespace
{
// Ctrl+C handler for repro mode — signals stop without terminating the process
static volatile LONG g_reproStopRequested = 0;

static BOOL WINAPI ReproCtrlHandler(DWORD ctrlType)
{
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT)
    {
        InterlockedExchange(&g_reproStopRequested, 1);
        return TRUE; // Handled — don't terminate
    }
    return FALSE;
}
} // anonymous namespace

namespace KTDiag
{

CLIHandler::CLIHandler() = default;
CLIHandler::~CLIHandler() = default;

int CLIHandler::Run(int argc, wchar_t* argv[])
{
    if (!ParseArgs(argc, argv))
        return 2;

    if (m_options.showHelp)
    {
        PrintHelp();
        return 0;
    }

    if (m_options.showVersion)
    {
        PrintVersion();
        return 0;
    }

    return Execute();
}

bool CLIHandler::ParseArgs(int argc, wchar_t* argv[])
{
    // No arguments at all — show help
    if (argc <= 1)
    {
        m_options.showHelp = true;
        return true;
    }

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg(argv[i]);

        if (arg == L"--help" || arg == L"-h")
        {
            m_options.showHelp = true;
            return true;
        }
        else if (arg == L"--version")
        {
            m_options.showVersion = true;
            return true;
        }
        else if (arg == L"--config" && i + 1 < argc)
        {
            m_options.configPath = argv[++i];
        }
        else if (arg == L"--output" && i + 1 < argc)
        {
            m_options.outputPath = argv[++i];
        }
        else if (arg == L"--collectors" && i + 1 < argc)
        {
            std::wstring val(argv[++i]);
            if (val == L"all")
            {
                m_options.collectAll = true;
            }
            else
            {
                // Parse comma-separated list
                size_t start = 0;
                size_t pos = 0;
                while ((pos = val.find(L',', start)) != std::wstring::npos)
                {
                    m_options.collectors.insert(val.substr(start, pos - start));
                    start = pos + 1;
                }
                if (start < val.size())
                    m_options.collectors.insert(val.substr(start));
            }
        }
        else if (arg == L"--duration" && i + 1 < argc)
        {
            m_options.duration = _wtoi(argv[++i]);
        }
        else if (arg == L"--wpp-level" && i + 1 < argc)
        {
            m_options.wppLevel = _wtoi(argv[++i]);
            if (m_options.wppLevel < 1 || m_options.wppLevel > 5)
            {
                std::wcerr << L"Error: --wpp-level must be 1-5\n";
                return false;
            }
        }
        else if (arg == L"--product-log-level" && i + 1 < argc)
        {
            m_options.productLogLevel = argv[++i];
        }
        else if (arg == L"--enable-wpp")
        {
            m_options.enableWpp = true;
        }
        else if (arg == L"--disable-wpp")
        {
            m_options.disableWpp = true;
        }
        else if (arg == L"--enable-perf")
        {
            m_options.enablePerf = true;
        }
        else if (arg == L"--disable-perf")
        {
            m_options.disablePerf = true;
        }
        else if (arg == L"--repro")
        {
            m_options.reproMode = true;
        }
        else if (arg == L"--analyze-dumps")
        {
            m_options.analyzeDumps = true;
        }
        else if (arg == L"--timeout" && i + 1 < argc)
        {
            m_options.timeout = _wtoi(argv[++i]);
            if (m_options.timeout < 0)
            {
                std::wcerr << L"Error: --timeout must be a positive number of seconds\n";
                return false;
            }
        }
        else if (arg == L"--stop-event" && i + 1 < argc)
        {
            m_options.stopEvent = argv[++i];
        }
        else if (arg == L"--quiet")
        {
            m_options.quiet = true;
        }
        else
        {
            std::wcerr << L"Unknown argument: " << arg << L"\n";
            std::wcerr << L"Use --help for usage information.\n";
            return false;
        }
    }

    // Validate required args for collection
    if (!m_options.showHelp && !m_options.showVersion && m_options.configPath.empty())
    {
        std::wcerr << L"Error: --config is required.\n";
        std::wcerr << L"Use --help for usage information.\n";
        return false;
    }

    return true;
}

void CLIHandler::PrintHelp()
{
    std::wcout <<
        L"KT-WinDiagTool v1.0.0 - Security Product Diagnostics Collector\n"
        L"\n"
        L"Usage:\n"
        L"  KT-WinDiagTool.exe --config <path> [options]\n"
        L"\n"
        L"Required:\n"
        L"  --config <path>           Path to product JSON config file\n"
        L"\n"
        L"Options:\n"
        L"  --output <path>           Output directory (default: Desktop\\KT-WinDiag_YYYYMMDD_HHMMSS_PID)\n"
        L"  --collectors <list>       Comma-separated collectors or 'all' (default: all)\n"
        L"    Available: wpp,etw,eventlog,perf,packets,dumps,registry,env,installed,\n"
        L"               productlogs,installlogs,services,proxy,network,firewall,\n"
        L"               userinfo,disk,wer,sysinfo\n"
        L"  --duration <seconds>      Duration for timed captures (default: 60)\n"
        L"  --wpp-level <1-5>         WPP trace level: 1=Critical..5=Verbose (default: 3)\n"
        L"  --product-log-level <lvl> Set product log verbosity via registry\n"
        L"  --enable-wpp              Start WPP tracing before collection\n"
        L"  --disable-wpp             Stop WPP tracing\n"
        L"  --enable-perf             Start performance logging before collection\n"
        L"  --disable-perf            Stop performance logging\n"
        L"  --repro                   Repro mode: start traces, wait for Ctrl+C or --stop-event, stop & collect\n"
        L"  --stop-event <name>       Named Win32 event to signal repro stop (for programmatic launch)\n"
        L"  --analyze-dumps           Run crash dump analysis\n"
        L"  --timeout <seconds>       Overall collection timeout; cancels and exits after N seconds\n"
        L"  --quiet                   Minimal output\n"
        L"  --version                 Print version and exit\n"
        L"  --help, -h                Show this help message\n"
        L"\n"
        L"Examples:\n"
        L"  KT-WinDiagTool.exe --config config\\sample_product.json\n"
        L"  KT-WinDiagTool.exe --config myav.json --collectors env,registry,sysinfo\n"
        L"  KT-WinDiagTool.exe --config myav.json --repro --wpp-level 5\n"
        L"\n";
}

void CLIHandler::PrintVersion()
{
    std::wcout << L"KT-WinDiagTool v1.0.0\n";
}

int CLIHandler::Execute()
{
    if (!m_options.quiet)
        std::wcout << L"KT-WinDiagTool v1.0.0 - Starting diagnostics collection...\n";

    // Load config
    Config config;
    if (!config.LoadFromFile(m_options.configPath))
    {
        std::wcerr << L"Error: Failed to load config: " << m_options.configPath << L"\n";
        return 2;
    }

    if (!m_options.quiet)
        std::wcout << L"Loaded product config: " << config.GetProductName() << L"\n";

    // Apply CLI --duration override to ALL timed collectors (ETW, WPP, packets)
    if (m_options.duration > 0)
    {
        config.SetCaptureDurationSeconds(m_options.duration);
        config.SetPacketCaptureDuration(m_options.duration);
    }

    // Set default output path if not specified.
    // Folder name uses the same YYYYMMDD_HHMMSS_PID suffix as the log file
    // so each run's output directory correlates directly with its log.
    if (m_options.outputPath.empty())
    {
        wchar_t desktop[MAX_PATH] = {};
        SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop);

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t folderName[64] = {};
        swprintf_s(folderName, L"KT-WinDiag_%04d%02d%02d_%02d%02d%02d_%lu",
                   st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute, st.wSecond,
                   GetCurrentProcessId());

        m_options.outputPath = std::wstring(desktop) + L"\\" + folderName;
    }

    if (!m_options.quiet)
        std::wcout << L"Output directory: " << m_options.outputPath << L"\n";

    // Create output directory
    std::filesystem::path outputDir(m_options.outputPath);
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec)
    {
        std::wcerr << L"Error: Cannot create output directory: " << m_options.outputPath << L"\n";
        return 2;
    }

    // --- Repro Mode ---
    if (m_options.reproMode)
    {
        // Handle --product-log-level before starting traces
        if (!m_options.productLogLevel.empty())
        {
            if (!m_options.quiet)
                std::wcout << L"Setting product log level to: " << m_options.productLogLevel << L"\n";
            if (!WppController::SetProductLogLevel(config, m_options.productLogLevel))
                std::wcerr << L"Warning: Failed to set product log level.\n";
        }

        ReproOrchestrator orchestrator;
        if (!m_options.quiet)
            std::wcout << L"\n=== REPRO MODE ===\n\n";

        auto startResult = orchestrator.StartRepro(config, outputDir, m_options.wppLevel);

        if (!m_options.quiet)
        {
            if (startResult.wppStarted)    std::wcout << L"  [OK] WPP tracing started\n";
            if (startResult.etwStarted)    std::wcout << L"  [OK] ETW tracing started\n";
            if (startResult.perfStarted)   std::wcout << L"  [OK] Performance logging started\n";
            if (startResult.packetStarted) std::wcout << L"  [OK] Packet capture started\n";
            for (const auto& err : startResult.errors)
                std::wcerr << L"  [WARN] " << err << L"\n";

            std::wcout << L"\nReproduce the issue now. Press Ctrl+C to stop traces and collect data...\n";
        }

        // Wait for Ctrl+C using a console control handler.
        // GUI subsystem apps share the console input buffer with cmd.exe,
        // making ENTER-based input unreliable. Ctrl+C is delivered via a
        // separate mechanism that doesn't race with the parent process.
        std::wcout.flush();
        InterlockedExchange(&g_reproStopRequested, 0);
        SetConsoleCtrlHandler(ReproCtrlHandler, TRUE);

        // Open the named stop event if one was provided (programmatic launch).
        // The calling process signals this event instead of sending Ctrl+C.
        HANDLE hStopEvent = nullptr;
        if (!m_options.stopEvent.empty())
        {
            hStopEvent = OpenEventW(SYNCHRONIZE, FALSE, m_options.stopEvent.c_str());
            if (!hStopEvent && !m_options.quiet)
                std::wcerr << L"Warning: cannot open stop event '"
                           << m_options.stopEvent << L"' (error "
                           << GetLastError() << L")\n";
        }

        while (!InterlockedCompareExchange(&g_reproStopRequested, 0, 0))
        {
            if (hStopEvent && WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0)
            {
                InterlockedExchange(&g_reproStopRequested, 1);
                break;
            }
            Sleep(200);
        }

        if (hStopEvent)
            CloseHandle(hStopEvent);

        SetConsoleCtrlHandler(ReproCtrlHandler, FALSE);

        if (!m_options.quiet)
            std::wcout << L"\nStopping traces...\n";

        orchestrator.StopRepro();

        if (!m_options.quiet)
            std::wcout << L"Traces stopped. Running collectors...\n\n";

        // Fall through to normal collection below
    }

    // Build collector engine and register all collectors
    CollectorEngine engine;
    engine.AddCollector(std::make_unique<EnvVarCollector>());
    engine.AddCollector(std::make_unique<RegistryCollector>());
    engine.AddCollector(std::make_unique<InstalledProductsCollector>());
    engine.AddCollector(std::make_unique<ProductLogCollector>());
    engine.AddCollector(std::make_unique<InstallLogCollector>());
    engine.AddCollector(std::make_unique<ServicesCollector>());
    engine.AddCollector(std::make_unique<ProxySettingsCollector>());
    engine.AddCollector(std::make_unique<NetworkConfigCollector>());
    engine.AddCollector(std::make_unique<FirewallConfigCollector>());
    engine.AddCollector(std::make_unique<UserInfoCollector>());
    engine.AddCollector(std::make_unique<DiskSpaceCollector>());
    engine.AddCollector(std::make_unique<WerCollector>());
    engine.AddCollector(std::make_unique<SystemInfoCollector>());
    engine.AddCollector(std::make_unique<EventLogCollector>());
    engine.AddCollector(std::make_unique<EtwLogCollector>());
    engine.AddCollector(std::make_unique<WppLogCollector>());
    engine.AddCollector(std::make_unique<PerfLogCollector>());
    engine.AddCollector(std::make_unique<PacketCaptureCollector>());
    engine.AddCollector(std::make_unique<CrashDumpCollector>());

    // Determine filter set: empty means run all
    std::set<std::wstring> filterIds;
    if (!m_options.collectAll)
        filterIds = m_options.collectors;

    // Progress callbacks
    EngineProgressCallback engineCb = nullptr;
    ProgressCallback collectorCb = nullptr;

    if (!m_options.quiet)
    {
        engineCb = [](int completed, int total, const std::wstring& name) {
            std::wcout << L"[" << completed << L"/" << total << L"] "
                       << name << L"...\n";
        };

        collectorCb = [](const std::wstring& name, const std::wstring& status, int /*pct*/) {
            std::wcout << L"  " << name << L": " << status << L"\n";
        };
    }

    // Handle --enable-wpp / --enable-perf (skip in repro mode — orchestrator handles them)
    std::unique_ptr<WppController> wppController;
    std::unique_ptr<PerfController> perfController;

    if (!m_options.reproMode)
    {
        // Handle --enable-wpp: start WPP tracing before collection
        if (m_options.enableWpp)
        {
            wppController = std::make_unique<WppController>();
            if (!m_options.quiet)
                std::wcout << L"Starting WPP tracing (level " << m_options.wppLevel << L")...\n";

            if (wppController->Start(config, m_options.wppLevel, outputDir))
            {
                if (!m_options.quiet)
                    std::wcout << L"WPP tracing started successfully.\n";
            }
            else
            {
                std::wcerr << L"Warning: WPP start failed: " << wppController->GetError() << L"\n";
                wppController.reset();
            }
        }

        // Handle --product-log-level: set product log verbosity via registry
        if (!m_options.productLogLevel.empty())
        {
            if (!m_options.quiet)
                std::wcout << L"Setting product log level to: " << m_options.productLogLevel << L"\n";

            if (!WppController::SetProductLogLevel(config, m_options.productLogLevel))
                std::wcerr << L"Warning: Failed to set product log level.\n";
        }

        // Handle --enable-perf: start perf counter logging before collection
        if (m_options.enablePerf)
        {
            perfController = std::make_unique<PerfController>();
            if (!m_options.quiet)
                std::wcout << L"Starting performance counter logging...\n";

            if (perfController->Start(config, 1000, outputDir))
            {
                if (!m_options.quiet)
                    std::wcout << L"Performance logging started successfully.\n";
            }
            else
            {
                std::wcerr << L"Warning: Perf start failed: " << perfController->GetError() << L"\n";
                perfController.reset();
            }
        }
    }

    if (!m_options.quiet)
        std::wcout << L"\nRunning collectors...\n\n";

    // Run the engine — optionally bounded by --timeout
    bool allOk = false;
    if (m_options.timeout > 0)
    {
        // Run on a background thread so we can enforce a wall-clock limit.
        auto engineFuture = std::async(std::launch::async, [&]() {
            return engine.RunAll(config, outputDir, filterIds, engineCb, collectorCb);
        });

        if (engineFuture.wait_for(std::chrono::seconds(m_options.timeout))
                == std::future_status::timeout)
        {
            if (!m_options.quiet)
                std::wcerr << L"\nTimeout (" << m_options.timeout
                           << L"s) reached — cancelling collection...\n";
            engine.Cancel();
        }
        allOk = engineFuture.get();
    }
    else
    {
        allOk = engine.RunAll(config, outputDir, filterIds, engineCb, collectorCb);
    }

    // Handle --disable-wpp or stop WPP after collection if --enable-wpp was used
    if (!m_options.reproMode)
    {
        if (m_options.disableWpp || (wppController && wppController->IsActive()))
        {
            if (wppController && wppController->IsActive())
            {
                if (!m_options.quiet)
                    std::wcout << L"\nStopping WPP tracing...\n";
                wppController->Stop();
            }
        }

        // Handle --disable-perf or stop perf after collection if --enable-perf was used
        if (m_options.disablePerf || (perfController && perfController->IsActive()))
        {
            if (perfController && perfController->IsActive())
            {
                if (!m_options.quiet)
                    std::wcout << L"Stopping performance logging...\n";
                perfController->Stop();
            }
        }
    }

    // Print results summary
    if (!m_options.quiet)
    {
        std::wcout << L"\n========================================\n";
        std::wcout << L"  Collection Results\n";
        std::wcout << L"========================================\n\n";

        for (const auto& result : engine.GetResults())
        {
            std::wcout << L"  " << (result.success ? L"[OK]  " : L"[FAIL]")
                       << L"  " << result.collectorName;
            std::wcout << std::fixed << std::setprecision(1);
            std::wcout << L"  (" << result.elapsedSeconds << L"s)";
            if (!result.success && !result.errorMessage.empty())
                std::wcout << L"  - " << result.errorMessage;
            std::wcout << L"\n";
        }

        std::wcout << L"\n  Passed: " << engine.SuccessCount()
                   << L"  Failed: " << engine.FailureCount()
                   << L"\n";
        std::wcout << L"  Output: " << m_options.outputPath << L"\n\n";
    }

    // --- Crash Dump Analysis (--analyze-dumps) ---
    if (m_options.analyzeDumps)
    {
        std::filesystem::path dumpsDir = outputDir / L"dumps";
        if (std::filesystem::exists(dumpsDir))
        {
            // Find .dmp files in the dumps output directory
            std::vector<std::filesystem::path> dumpFiles;
            std::error_code scanEc;
            for (const auto& entry : std::filesystem::directory_iterator(dumpsDir, scanEc))
            {
                if (entry.is_regular_file())
                {
                    auto ext = entry.path().extension().wstring();
                    std::wstring extLower = ext;
                    for (auto& ch : extLower) ch = towlower(ch);
                    if (extLower == L".dmp")
                        dumpFiles.push_back(entry.path());
                }
            }

            if (!dumpFiles.empty())
            {
                if (!m_options.quiet)
                    std::wcout << L"\n--- Crash Dump Analysis ---\n";

                // Built-in DbgHelp analysis
                std::filesystem::path analysisDir = outputDir / L"dump_analysis";
                std::filesystem::create_directories(analysisDir, scanEc);

                DumpAnalyzer analyzer;
                for (const auto& dumpFile : dumpFiles)
                {
                    if (!m_options.quiet)
                        std::wcout << L"  Analyzing: " << dumpFile.filename().wstring() << L"\n";

                    auto result = analyzer.Analyze(dumpFile);
                    std::filesystem::path reportFile = analysisDir /
                        (dumpFile.stem().wstring() + L"_analysis.txt");
                    DumpAnalyzer::WriteReport(result, reportFile);

                    if (!m_options.quiet)
                    {
                        if (result.success)
                        {
                            std::wcout << L"    Exception: " << result.exceptionCode;
                            if (!result.faultingModule.empty())
                                std::wcout << L" in " << result.faultingModule;
                            std::wcout << L"\n";
                        }
                        else
                        {
                            std::wcout << L"    Analysis error: " << result.error << L"\n";
                        }
                    }
                }

                // WinDbg automation (optional)
                std::filesystem::path cdbPath = WinDbgAutomation::FindCdb();
                if (!cdbPath.empty())
                {
                    if (!m_options.quiet)
                        std::wcout << L"\n  Running WinDbg analysis (cdb.exe found)...\n";

                    std::filesystem::path windbgDir = outputDir / L"windbg_analysis";
                    std::filesystem::create_directories(windbgDir, scanEc);

                    WinDbgAutomation windbg;
                    for (const auto& dumpFile : dumpFiles)
                    {
                        if (!m_options.quiet)
                            std::wcout << L"    WinDbg: " << dumpFile.filename().wstring() << L"\n";

                        std::filesystem::path outFile = windbgDir /
                            (dumpFile.stem().wstring() + L"_windbg.txt");
                        if (!windbg.Analyze(dumpFile, outFile))
                        {
                            Logger::Instance().Log(LogLevel::Warning,
                                L"[WinDbg] Analysis failed for %s: %s",
                                dumpFile.wstring().c_str(), windbg.GetError().c_str());
                        }
                    }
                }
                else
                {
                    // Write not-found guidance
                    WinDbgAutomation::WriteNotFoundReport(outputDir / L"windbg_not_found.txt");
                    if (!m_options.quiet)
                        std::wcout << L"\n  WinDbg/cdb.exe not found (see windbg_not_found.txt)\n";
                }

                if (!m_options.quiet)
                    std::wcout << L"  Analysis complete.\n";
            }
            else
            {
                if (!m_options.quiet)
                    std::wcout << L"\nNo crash dumps found to analyze.\n";
            }
        }
        else
        {
            if (!m_options.quiet)
                std::wcout << L"\nNo dumps directory found. Run with --collectors dumps first.\n";
        }
    }

    // --- Generate Summary Report ---
    {
        ReportGenerator reportGen;
        if (reportGen.Generate(outputDir, config, engine.GetResults()))
        {
            if (!m_options.quiet)
                std::wcout << L"\nSummary report: " << (outputDir / L"summary.html").wstring() << L"\n";
        }
        else
        {
            std::wcerr << L"Warning: Report generation failed: " << reportGen.GetError() << L"\n";
        }
    }

    // --- Package Output into ZIP ---
    {
        ZipPackager packager;
        if (packager.Package(outputDir, config.GetProductName()))
        {
            if (!m_options.quiet)
                std::wcout << L"ZIP package:    " << packager.GetZipPath() << L"\n";
        }
        else
        {
            std::wcerr << L"Warning: ZIP packaging failed: " << packager.GetError() << L"\n";
        }
    }

    // --- Write machine-readable result.json ---
    // Lets the calling process parse per-collector pass/fail without screen-scraping.
    {
        int exitCode = allOk ? 0 : (engine.SuccessCount() > 0 ? 1 : 2);

        std::filesystem::path resultFile = outputDir / L"result.json";
        std::wofstream rj(resultFile);
        if (rj.is_open())
        {
            // Timestamp (UTC ISO-8601)
            SYSTEMTIME utc;
            GetSystemTime(&utc);
            wchar_t ts[32];
            swprintf_s(ts, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
                       utc.wYear, utc.wMonth, utc.wDay,
                       utc.wHour, utc.wMinute, utc.wSecond);

            auto escJson = [](const std::wstring& s) -> std::wstring {
                std::wstring out;
                for (wchar_t c : s)
                {
                    if      (c == L'"')  out += L"\\\"";
                    else if (c == L'\\') out += L"\\\\";
                    else if (c == L'\n') out += L"\\n";
                    else if (c == L'\r') out += L"\\r";
                    else                 out += c;
                }
                return out;
            };

            rj << L"{\n";
            rj << L"  \"product\": \""      << escJson(config.GetProductName()) << L"\",\n";
            rj << L"  \"timestamp_utc\": \"" << ts << L"\",\n";
            rj << L"  \"pid\": "            << GetCurrentProcessId() << L",\n";
            rj << L"  \"log_file\": \""     << escJson(Logger::Instance().GetLogFilePath()) << L"\",\n";
            rj << L"  \"output_dir\": \""   << escJson(outputDir.wstring()) << L"\",\n";
            rj << L"  \"exit_code\": "      << exitCode << L",\n";
            rj << L"  \"collectors\": [\n";

            const auto& results = engine.GetResults();
            for (size_t i = 0; i < results.size(); ++i)
            {
                const auto& r = results[i];
                rj << L"    {"
                   << L"\"id\": \""      << escJson(r.collectorId)   << L"\", "
                   << L"\"name\": \""    << escJson(r.collectorName) << L"\", "
                   << L"\"success\": "   << (r.success ? L"true" : L"false") << L", "
                   << L"\"elapsed_seconds\": " << r.elapsedSeconds << L", "
                   << L"\"error\": \""   << escJson(r.errorMessage) << L"\""
                   << L"}";
                if (i + 1 < results.size()) rj << L",";
                rj << L"\n";
            }

            rj << L"  ],\n";
            rj << L"  \"summary\": {"
               << L"\"total\": "  << results.size()          << L", "
               << L"\"passed\": " << engine.SuccessCount()   << L", "
               << L"\"failed\": " << engine.FailureCount()
               << L"}\n";
            rj << L"}\n";
            rj.close();

            if (!m_options.quiet)
                std::wcout << L"Result JSON:    " << resultFile.wstring() << L"\n";
        }
    }

    // Exit code: 0=all success, 1=partial failure, 2=all failed
    if (allOk)
        return 0;
    return (engine.SuccessCount() > 0) ? 1 : 2;
}

} // namespace KTDiag
