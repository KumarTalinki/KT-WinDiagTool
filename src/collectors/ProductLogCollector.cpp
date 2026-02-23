#include "ProductLogCollector.h"
#include "../util/Logger.h"

#include <fstream>

namespace fs = std::filesystem;

namespace KTDiag
{

int ProductLogCollector::CopyDirectory(const fs::path& sourceDir,
                                        const fs::path& destDir,
                                        CancelToken cancel)
{
    int copied = 0;
    std::error_code ec;

    if (!fs::exists(sourceDir, ec))
        return 0;

    fs::create_directories(destDir, ec);

    for (const auto& entry : fs::recursive_directory_iterator(sourceDir,
             fs::directory_options::skip_permission_denied, ec))
    {
        if (cancel && cancel->load()) break;

        if (entry.is_regular_file())
        {
            fs::path relativePath = fs::relative(entry.path(), sourceDir, ec);
            fs::path destPath = destDir / relativePath;

            fs::create_directories(destPath.parent_path(), ec);

            if (fs::copy_file(entry.path(), destPath,
                              fs::copy_options::overwrite_existing, ec))
            {
                ++copied;
            }
            else
            {
                Logger::Instance().Log(LogLevel::Warning,
                    L"[ProductLogs] Failed to copy: %s (%S)",
                    entry.path().wstring().c_str(),
                    ec.message().c_str());
            }
        }
    }

    return copied;
}

bool ProductLogCollector::Collect(const Config& config,
                                   const fs::path& outputDir,
                                   ProgressCallback cb,
                                   CancelToken cancel)
{
    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    const auto& logPaths = config.GetLogPaths();
    if (logPaths.empty())
    {
        if (cb) cb(Name(), L"No product log paths configured", 100);
        Logger::Instance().Log(LogLevel::Warning, L"[ProductLogs] No log paths in config");
        return true;
    }

    if (cb) cb(Name(), L"Copying product logs...", 0);

    int totalCopied = 0;
    int totalPaths = static_cast<int>(logPaths.size());

    for (int i = 0; i < totalPaths; ++i)
    {
        if (cancel && cancel->load())
        {
            m_error = L"Cancelled";
            return false;
        }

        const auto& logPath = logPaths[i];
        int pct = (i * 100) / totalPaths;
        if (cb) cb(Name(), L"Copying from: " + logPath, pct);

        fs::path sourcePath(logPath);

        if (fs::is_directory(sourcePath))
        {
            // Copy entire directory
            std::wstring dirName = sourcePath.filename().wstring();
            if (dirName.empty()) dirName = L"logs_" + std::to_wstring(i);
            fs::path destDir = outputDir / dirName;

            int copied = CopyDirectory(sourcePath, destDir, cancel);
            totalCopied += copied;
            Logger::Instance().Log(LogLevel::Info, L"[ProductLogs] Copied %d files from %s",
                                   copied, logPath.c_str());
        }
        else if (fs::is_regular_file(sourcePath))
        {
            // Copy single file
            std::error_code ec;
            fs::path destFile = outputDir / sourcePath.filename();
            if (fs::copy_file(sourcePath, destFile, fs::copy_options::overwrite_existing, ec))
            {
                ++totalCopied;
            }
            else
            {
                Logger::Instance().Log(LogLevel::Warning,
                    L"[ProductLogs] Failed to copy file: %s", logPath.c_str());
            }
        }
        else
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[ProductLogs] Path not found: %s", logPath.c_str());
        }
    }

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[ProductLogs] Total files copied: %d", totalCopied);
    return true;
}

} // namespace KTDiag
