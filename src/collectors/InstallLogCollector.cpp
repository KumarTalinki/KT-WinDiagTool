#include "InstallLogCollector.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <shlwapi.h>

namespace fs = std::filesystem;

namespace KTDiag
{

bool InstallLogCollector::Collect(const Config& config,
                                   const fs::path& outputDir,
                                   ProgressCallback cb,
                                   CancelToken cancel)
{
    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    const auto& installPaths = config.GetInstallLogPaths();
    if (installPaths.empty())
    {
        if (cb) cb(Name(), L"No installation log paths configured", 100);
        Logger::Instance().Log(LogLevel::Warning, L"[InstallLogs] No install log paths in config");
        return true;
    }

    if (cb) cb(Name(), L"Copying installation logs...", 0);

    int copied = 0;
    int notFound = 0;
    int total = static_cast<int>(installPaths.size());

    for (int i = 0; i < total; ++i)
    {
        if (cancel && cancel->load())
        {
            m_error = L"Cancelled";
            return false;
        }

        const auto& logPath = installPaths[i];
        int pct = (i * 100) / total;
        if (cb) cb(Name(), L"Copying: " + logPath, pct);

        fs::path sourcePath(logPath);

        // Handle glob-like patterns: if path contains * or ?, iterate matching files
        std::wstring filename = sourcePath.filename().wstring();
        if (filename.find(L'*') != std::wstring::npos ||
            filename.find(L'?') != std::wstring::npos)
        {
            // Use parent directory + pattern matching
            fs::path parentDir = sourcePath.parent_path();
            if (fs::exists(parentDir))
            {
                std::error_code ec;
                for (const auto& entry : fs::directory_iterator(parentDir,
                         fs::directory_options::skip_permission_denied, ec))
                {
                    if (cancel && cancel->load()) break;
                    if (!entry.is_regular_file()) continue;

                    // Simple wildcard match using PathMatchSpecW
                    if (PathMatchSpecW(entry.path().filename().c_str(), filename.c_str()))
                    {
                        fs::path destFile = outputDir / entry.path().filename();
                        if (fs::copy_file(entry.path(), destFile,
                                          fs::copy_options::overwrite_existing, ec))
                        {
                            ++copied;
                        }
                    }
                }
            }
        }
        else if (fs::is_regular_file(sourcePath))
        {
            std::error_code ec;
            fs::path destFile = outputDir / sourcePath.filename();
            if (fs::copy_file(sourcePath, destFile,
                              fs::copy_options::overwrite_existing, ec))
            {
                ++copied;
            }
            else
            {
                Logger::Instance().Log(LogLevel::Warning,
                    L"[InstallLogs] Failed to copy: %s", logPath.c_str());
            }
        }
        else
        {
            ++notFound;
            Logger::Instance().Log(LogLevel::Info,
                L"[InstallLogs] Not found (product may not be installed): %s",
                logPath.c_str());
        }
    }

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info,
        L"[InstallLogs] Copied %d file(s), %d not found", copied, notFound);
    return true;
}

} // namespace KTDiag
