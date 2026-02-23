#include "DumpAnalyzer.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <DbgHelp.h>
#include <fstream>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "dbghelp.lib")

namespace
{

std::wstring FormatHex64(ULONG64 value)
{
    std::wostringstream oss;
    oss << L"0x" << std::hex << std::uppercase << std::setfill(L'0') << std::setw(16) << value;
    return oss.str();
}

std::wstring FormatHex32(DWORD value)
{
    std::wostringstream oss;
    oss << L"0x" << std::hex << std::uppercase << std::setfill(L'0') << std::setw(8) << value;
    return oss.str();
}

} // anonymous namespace

namespace KTDiag
{

DumpAnalysisResult DumpAnalyzer::Analyze(const std::filesystem::path& dumpFile)
{
    DumpAnalysisResult result;
    result.dumpPath = dumpFile.wstring();

    // Open the dump file
    HANDLE hFile = CreateFileW(dumpFile.wstring().c_str(), GENERIC_READ,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        result.error = L"Failed to open dump file (error " +
                       std::to_wstring(GetLastError()) + L")";
        return result;
    }

    // Create file mapping
    HANDLE hMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMapping)
    {
        result.error = L"Failed to create file mapping (error " +
                       std::to_wstring(GetLastError()) + L")";
        CloseHandle(hFile);
        return result;
    }

    // Map view
    LPVOID pView = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!pView)
    {
        result.error = L"Failed to map view of file (error " +
                       std::to_wstring(GetLastError()) + L")";
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return result;
    }

    // Validate minidump signature before reading any streams
    {
        auto* header = static_cast<MINIDUMP_HEADER*>(pView);
        if (header->Signature != MINIDUMP_SIGNATURE)
        {
            result.error = L"Not a valid minidump file (invalid signature)";
            UnmapViewOfFile(pView);
            CloseHandle(hMapping);
            CloseHandle(hFile);
            return result;
        }
    }

    // --- Read System Info Stream ---
    {
        MINIDUMP_DIRECTORY* streamDir = nullptr;
        PVOID streamData = nullptr;
        ULONG streamSize = 0;

        if (MiniDumpReadDumpStream(pView, SystemInfoStream, &streamDir,
                                    &streamData, &streamSize))
        {
            auto* sysInfo = static_cast<MINIDUMP_SYSTEM_INFO*>(streamData);
            std::wostringstream oss;
            oss << L"OS Version: " << sysInfo->MajorVersion << L"."
                << sysInfo->MinorVersion << L"." << sysInfo->BuildNumber;

            switch (sysInfo->ProcessorArchitecture)
            {
            case PROCESSOR_ARCHITECTURE_AMD64:
                oss << L" (x64)";
                break;
            case PROCESSOR_ARCHITECTURE_INTEL:
                oss << L" (x86)";
                break;
            case PROCESSOR_ARCHITECTURE_ARM64:
                oss << L" (ARM64)";
                break;
            default:
                oss << L" (arch=" << sysInfo->ProcessorArchitecture << L")";
                break;
            }

            oss << L", Processors: " << sysInfo->NumberOfProcessors;
            result.systemInfo = oss.str();
        }
    }

    // --- Read Exception Stream ---
    CONTEXT exceptionContext = {};
    bool hasException = false;
    DWORD exceptionThreadId = 0;

    {
        MINIDUMP_DIRECTORY* streamDir = nullptr;
        PVOID streamData = nullptr;
        ULONG streamSize = 0;

        if (MiniDumpReadDumpStream(pView, ExceptionStream, &streamDir,
                                    &streamData, &streamSize))
        {
            auto* excStream = static_cast<MINIDUMP_EXCEPTION_STREAM*>(streamData);
            auto& excRecord = excStream->ExceptionRecord;

            result.exceptionCode = FormatHex32(excRecord.ExceptionCode);
            result.exceptionAddress = FormatHex64(excRecord.ExceptionAddress);
            exceptionThreadId = excStream->ThreadId;

            // Get the thread context for stack walking
            if (excStream->ThreadContext.DataSize >= sizeof(CONTEXT))
            {
                auto* pCtx = reinterpret_cast<CONTEXT*>(
                    static_cast<BYTE*>(pView) + excStream->ThreadContext.Rva);
                memcpy(&exceptionContext, pCtx, sizeof(CONTEXT));
                hasException = true;
            }
        }
    }

    // --- Read Module List Stream ---
    ULONG64 exceptionAddr = 0;
    if (!result.exceptionAddress.empty())
    {
        // Parse the hex address back for module matching
        exceptionAddr = wcstoull(result.exceptionAddress.c_str() + 2, nullptr, 16);
    }

    {
        MINIDUMP_DIRECTORY* streamDir = nullptr;
        PVOID streamData = nullptr;
        ULONG streamSize = 0;

        if (MiniDumpReadDumpStream(pView, ModuleListStream, &streamDir,
                                    &streamData, &streamSize))
        {
            auto* modList = static_cast<MINIDUMP_MODULE_LIST*>(streamData);

            for (ULONG32 i = 0; i < modList->NumberOfModules; ++i)
            {
                const auto& mod = modList->Modules[i];

                // Get module name from RVA
                std::wstring moduleName = L"<unknown>";
                if (mod.ModuleNameRva != 0)
                {
                    auto* nameStr = reinterpret_cast<MINIDUMP_STRING*>(
                        static_cast<BYTE*>(pView) + mod.ModuleNameRva);
                    moduleName = std::wstring(nameStr->Buffer,
                                              nameStr->Length / sizeof(wchar_t));
                }

                std::wostringstream entry;
                entry << FormatHex64(mod.BaseOfImage)
                      << L"  " << FormatHex64(mod.BaseOfImage + mod.SizeOfImage)
                      << L"  " << moduleName;

                // Version info
                auto& vi = mod.VersionInfo;
                if (vi.dwFileVersionMS != 0 || vi.dwFileVersionLS != 0)
                {
                    entry << L"  v" << HIWORD(vi.dwFileVersionMS) << L"."
                          << LOWORD(vi.dwFileVersionMS) << L"."
                          << HIWORD(vi.dwFileVersionLS) << L"."
                          << LOWORD(vi.dwFileVersionLS);
                }

                result.loadedModules.push_back(entry.str());

                // Check if this module contains the exception address
                if (exceptionAddr >= mod.BaseOfImage &&
                    exceptionAddr < mod.BaseOfImage + mod.SizeOfImage)
                {
                    // Extract just the filename
                    std::filesystem::path modPath(moduleName);
                    result.faultingModule = modPath.filename().wstring();
                    result.faultingOffset = FormatHex64(exceptionAddr - mod.BaseOfImage);
                }
            }
        }
    }

    // --- Stack Walk (best-effort) ---
    if (hasException)
    {
        HANDLE hProcess = GetCurrentProcess();

        // Initialize symbol handler with the dump file
        if (SymInitialize(hProcess, nullptr, FALSE))
        {
            // Enumerate modules from dump for symbol resolution
            {
                MINIDUMP_DIRECTORY* streamDir = nullptr;
                PVOID streamData = nullptr;
                ULONG streamSize = 0;

                if (MiniDumpReadDumpStream(pView, ModuleListStream, &streamDir,
                                            &streamData, &streamSize))
                {
                    auto* modList = static_cast<MINIDUMP_MODULE_LIST*>(streamData);
                    for (ULONG32 i = 0; i < modList->NumberOfModules; ++i)
                    {
                        const auto& mod = modList->Modules[i];
                        std::wstring moduleName;
                        if (mod.ModuleNameRva != 0)
                        {
                            auto* nameStr = reinterpret_cast<MINIDUMP_STRING*>(
                                static_cast<BYTE*>(pView) + mod.ModuleNameRva);
                            moduleName = std::wstring(nameStr->Buffer,
                                                      nameStr->Length / sizeof(wchar_t));
                        }

                        SymLoadModuleExW(hProcess, nullptr, moduleName.c_str(),
                                          nullptr, mod.BaseOfImage, mod.SizeOfImage,
                                          nullptr, 0);
                    }
                }
            }

            STACKFRAME64 stackFrame = {};
            DWORD machineType = 0;

#ifdef _M_AMD64
            machineType = IMAGE_FILE_MACHINE_AMD64;
            stackFrame.AddrPC.Offset = exceptionContext.Rip;
            stackFrame.AddrPC.Mode = AddrModeFlat;
            stackFrame.AddrFrame.Offset = exceptionContext.Rbp;
            stackFrame.AddrFrame.Mode = AddrModeFlat;
            stackFrame.AddrStack.Offset = exceptionContext.Rsp;
            stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86)
            machineType = IMAGE_FILE_MACHINE_I386;
            stackFrame.AddrPC.Offset = exceptionContext.Eip;
            stackFrame.AddrPC.Mode = AddrModeFlat;
            stackFrame.AddrFrame.Offset = exceptionContext.Ebp;
            stackFrame.AddrFrame.Mode = AddrModeFlat;
            stackFrame.AddrStack.Offset = exceptionContext.Esp;
            stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64)
            machineType = IMAGE_FILE_MACHINE_ARM64;
            stackFrame.AddrPC.Offset = exceptionContext.Pc;
            stackFrame.AddrPC.Mode = AddrModeFlat;
            stackFrame.AddrFrame.Offset = exceptionContext.Fp;
            stackFrame.AddrFrame.Mode = AddrModeFlat;
            stackFrame.AddrStack.Offset = exceptionContext.Sp;
            stackFrame.AddrStack.Mode = AddrModeFlat;
#endif

            constexpr int MAX_FRAMES = 64;
            for (int frame = 0; frame < MAX_FRAMES; ++frame)
            {
                if (!StackWalk64(machineType, hProcess, nullptr,
                                  &stackFrame, &exceptionContext,
                                  nullptr, SymFunctionTableAccess64,
                                  SymGetModuleBase64, nullptr))
                {
                    break;
                }

                if (stackFrame.AddrPC.Offset == 0)
                    break;

                std::wostringstream frameStr;
                frameStr << L"  " << std::setw(2) << frame << L": "
                         << FormatHex64(stackFrame.AddrPC.Offset);

                // Try to resolve symbol name
                BYTE symbolBuffer[sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(wchar_t)] = {};
                auto* symbolInfo = reinterpret_cast<SYMBOL_INFOW*>(symbolBuffer);
                symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFOW);
                symbolInfo->MaxNameLen = MAX_SYM_NAME;
                DWORD64 displacement = 0;

                if (SymFromAddrW(hProcess, stackFrame.AddrPC.Offset,
                                  &displacement, symbolInfo))
                {
                    frameStr << L"  " << symbolInfo->Name
                             << L"+0x" << std::hex << displacement;
                }
                else
                {
                    // Try module+offset
                    IMAGEHLP_MODULEW64 modInfo = {};
                    modInfo.SizeOfStruct = sizeof(modInfo);
                    if (SymGetModuleInfoW64(hProcess, stackFrame.AddrPC.Offset, &modInfo))
                    {
                        frameStr << L"  " << modInfo.ModuleName
                                 << L"+0x" << std::hex
                                 << (stackFrame.AddrPC.Offset - modInfo.BaseOfImage);
                    }
                }

                result.callStack.push_back(frameStr.str());
            }

            SymCleanup(hProcess);
        }
    }

    // Cleanup mapping
    UnmapViewOfFile(pView);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    result.success = true;
    return result;
}

void DumpAnalyzer::WriteReport(const DumpAnalysisResult& result,
                                const std::filesystem::path& outputFile)
{
    std::wofstream ofs(outputFile);
    if (!ofs)
        return;

    ofs << L"Crash Dump Analysis Report\n";
    ofs << L"==========================\n\n";

    ofs << L"Dump File: " << result.dumpPath << L"\n\n";

    if (!result.success)
    {
        ofs << L"Analysis failed: " << result.error << L"\n";
        return;
    }

    // --- Summary ---
    ofs << L"--- Summary ---\n";
    if (!result.systemInfo.empty())
        ofs << L"System:          " << result.systemInfo << L"\n";
    if (!result.processName.empty())
        ofs << L"Process:         " << result.processName << L"\n";
    if (!result.exceptionCode.empty())
        ofs << L"Exception Code:  " << result.exceptionCode << L"\n";
    if (!result.exceptionAddress.empty())
        ofs << L"Exception Addr:  " << result.exceptionAddress << L"\n";
    if (!result.faultingModule.empty())
    {
        ofs << L"Faulting Module: " << result.faultingModule << L"\n";
        ofs << L"Faulting Offset: " << result.faultingOffset << L"\n";
    }
    ofs << L"\n";

    // --- Stack Trace ---
    ofs << L"--- Stack Trace ---\n";
    if (result.callStack.empty())
    {
        ofs << L"  (No stack trace available — symbols may not be loaded)\n";
    }
    else
    {
        for (const auto& frame : result.callStack)
            ofs << frame << L"\n";
    }
    ofs << L"\n";

    // --- Loaded Modules ---
    ofs << L"--- Loaded Modules (" << result.loadedModules.size() << L") ---\n";
    for (const auto& mod : result.loadedModules)
        ofs << L"  " << mod << L"\n";
    ofs << L"\n";
}

} // namespace KTDiag
