#include "EnvVarCollector.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <fstream>
#include <vector>
#include <algorithm>

namespace KTDiag
{

bool EnvVarCollector::Collect(const Config& config,
                               const std::filesystem::path& outputDir,
                               ProgressCallback cb,
                               CancelToken cancel)
{
    UNREFERENCED_PARAMETER(config);
    if (cb) cb(Name(), L"Collecting environment variables...", 0);

    LPWCH envBlock = GetEnvironmentStringsW();
    if (!envBlock)
    {
        m_error = L"GetEnvironmentStringsW failed";
        return false;
    }

    // Parse the environment block (double-null terminated, each entry null-terminated)
    std::vector<std::wstring> vars;
    LPCWSTR ptr = envBlock;
    while (*ptr)
    {
        std::wstring entry(ptr);
        // Skip internal entries that start with '='
        if (!entry.empty() && entry[0] != L'=')
            vars.push_back(entry);
        ptr += entry.size() + 1;
    }
    FreeEnvironmentStringsW(envBlock);

    // Sort alphabetically
    std::sort(vars.begin(), vars.end(),
              [](const std::wstring& a, const std::wstring& b)
              { return _wcsicmp(a.c_str(), b.c_str()) < 0; });

    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    if (cb) cb(Name(), L"Writing output...", 50);

    // Write to file
    std::filesystem::path outFile = outputDir / L"env_vars.txt";
    std::wofstream file(outFile);
    if (!file.is_open())
    {
        m_error = L"Failed to create output file: " + outFile.wstring();
        return false;
    }

    file << L"=== Environment Variables ===" << std::endl;
    file << L"Total: " << vars.size() << std::endl;
    file << L"========================================" << std::endl << std::endl;

    for (const auto& var : vars)
    {
        file << var << std::endl;
    }

    file.close();

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[EnvVar] Collected %zu environment variables",
                           vars.size());
    return true;
}

} // namespace KTDiag
