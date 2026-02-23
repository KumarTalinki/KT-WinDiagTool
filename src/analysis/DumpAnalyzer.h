#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace KTDiag
{

struct DumpAnalysisResult
{
    std::wstring dumpPath;
    std::wstring exceptionCode;
    std::wstring exceptionAddress;
    std::wstring faultingModule;
    std::wstring faultingOffset;
    std::vector<std::wstring> callStack;
    std::vector<std::wstring> loadedModules;
    std::wstring systemInfo;
    std::wstring processName;
    bool success = false;
    std::wstring error;
};

class DumpAnalyzer
{
public:
    DumpAnalysisResult Analyze(const std::filesystem::path& dumpFile);

    static void WriteReport(const DumpAnalysisResult& result,
                            const std::filesystem::path& outputFile);
};

} // namespace KTDiag
