#pragma once

#include <Windows.h>
#include <string>
#include <filesystem>

namespace KTDiag
{

class WinDbgAutomation
{
public:
    // Find cdb.exe on the system (PATH, Program Files, registry)
    static std::filesystem::path FindCdb();

    // Run !analyze -v on a dump file, write output to outputFile
    bool Analyze(const std::filesystem::path& dumpFile,
                 const std::filesystem::path& outputFile,
                 DWORD timeoutMs = 120000);

    // Write a guidance file when cdb.exe is not found
    static void WriteNotFoundReport(const std::filesystem::path& outputFile);

    std::wstring GetError() const { return m_error; }

private:
    bool RunCommand(const std::wstring& cmdLine, std::wstring& output, DWORD timeoutMs);
    std::wstring m_error;
};

} // namespace KTDiag
