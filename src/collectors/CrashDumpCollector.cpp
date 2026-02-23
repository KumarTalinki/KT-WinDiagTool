#include "CrashDumpCollector.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

namespace
{

constexpr int MAX_DUMP_AGE_DAYS = 30;
constexpr std::uintmax_t MAX_DUMP_SIZE_BYTES = 500ULL * 1024 * 1024; // 500 MB

bool ParseRegistryPath(const std::wstring& fullPath, HKEY& rootKey,
                       std::wstring& subKey, std::wstring& valueName)
{
    size_t firstSlash = fullPath.find(L'\\');
    if (firstSlash == std::wstring::npos)
        return false;

    std::wstring rootStr = fullPath.substr(0, firstSlash);
    std::wstring remainder = fullPath.substr(firstSlash + 1);

    if (rootStr == L"HKLM" || rootStr == L"HKEY_LOCAL_MACHINE")
        rootKey = HKEY_LOCAL_MACHINE;
    else if (rootStr == L"HKCU" || rootStr == L"HKEY_CURRENT_USER")
        rootKey = HKEY_CURRENT_USER;
    else
        return false;

    size_t lastSlash = remainder.rfind(L'\\');
    if (lastSlash == std::wstring::npos)
        return false;

    subKey = remainder.substr(0, lastSlash);
    valueName = remainder.substr(lastSlash + 1);
    return true;
}

// Make a safe filename from a full path to avoid collisions
std::wstring MakeSafeFilename(const fs::path& sourcePath, const std::wstring& label)
{
    std::wstring stem = sourcePath.stem().wstring();
    std::wstring ext = sourcePath.extension().wstring();
    return stem + L"_" + label + ext;
}

} // anonymous namespace

namespace KTDiag
{

std::vector<std::wstring> CrashDumpCollector::GatherScanPaths(const Config& config)
{
    std::vector<std::wstring> paths;

    // 1. Product-specific paths from config
    for (const auto& p : config.GetCrashDumpPaths())
    {
        std::wstring expanded = Config::ExpandEnvironmentPath(p);
        if (!expanded.empty())
            paths.push_back(expanded);
    }

    // 2. Windows kernel minidumps: %SystemRoot%\Minidump
    {
        wchar_t sysRoot[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"SystemRoot", sysRoot, MAX_PATH) > 0)
        {
            paths.push_back(std::wstring(sysRoot) + L"\\Minidump");
        }
    }

    // 3. WER user-mode crash dumps: %LocalAppData%\CrashDumps
    {
        wchar_t localAppData[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"LocalAppData", localAppData, MAX_PATH) > 0)
        {
            paths.push_back(std::wstring(localAppData) + L"\\CrashDumps");
        }
    }

    // 4. Registry-configured dump directory
    const auto& regPath = config.GetCrashDumpConfigRegistry();
    if (!regPath.empty())
    {
        std::wstring regDumpDir = ReadRegistryDumpPath(regPath);
        if (!regDumpDir.empty())
            paths.push_back(regDumpDir);
    }

    return paths;
}

std::wstring CrashDumpCollector::ReadRegistryDumpPath(const std::wstring& registryPath)
{
    HKEY rootKey;
    std::wstring subKey, valueName;
    if (!ParseRegistryPath(registryPath, rootKey, subKey, valueName))
        return L"";

    HKEY hKey;
    LONG result = RegOpenKeyExW(rootKey, subKey.c_str(), 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
        return L"";

    wchar_t buffer[MAX_PATH] = {};
    DWORD bufSize = sizeof(buffer);
    DWORD type = 0;
    result = RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type,
                              reinterpret_cast<BYTE*>(buffer), &bufSize);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS || type != REG_SZ)
        return L"";

    // Expand environment variables in the value
    wchar_t expanded[MAX_PATH] = {};
    DWORD expandedLen = ExpandEnvironmentStringsW(buffer, expanded, MAX_PATH);
    if (expandedLen > 0 && expandedLen <= MAX_PATH)
        return std::wstring(expanded);

    return std::wstring(buffer);
}

void CrashDumpCollector::ScanDirectory(const fs::path& dir,
                                        const std::wstring& sourceLabel,
                                        std::vector<DumpFileInfo>& results)
{
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return;

    auto now = fs::file_time_type::clock::now();
    auto maxAge = std::chrono::hours(24 * MAX_DUMP_AGE_DAYS);

    for (const auto& entry : fs::recursive_directory_iterator(dir,
             fs::directory_options::skip_permission_denied, ec))
    {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().wstring();
        // Case-insensitive .dmp check
        if (ext.size() != 4)
            continue;
        std::wstring extLower = ext;
        for (auto& ch : extLower) ch = towlower(ch);
        if (extLower != L".dmp")
            continue;

        // Check age
        auto lastWrite = entry.last_write_time(ec);
        if (ec)
            continue;
        auto age = now - lastWrite;
        if (age > maxAge)
        {
            Logger::Instance().Log(LogLevel::Info,
                L"[Dumps] Skipping old dump (>%d days): %s",
                MAX_DUMP_AGE_DAYS, entry.path().wstring().c_str());
            continue;
        }

        // Check size
        auto fileSize = entry.file_size(ec);
        if (ec)
            continue;
        if (fileSize > MAX_DUMP_SIZE_BYTES)
        {
            Logger::Instance().Log(LogLevel::Info,
                L"[Dumps] Skipping large dump (>500MB): %s (%llu bytes)",
                entry.path().wstring().c_str(), fileSize);
            continue;
        }

        DumpFileInfo info;
        info.sourcePath = entry.path();
        info.sourceLabel = sourceLabel;
        info.fileSize = fileSize;
        info.lastWrite = lastWrite;
        results.push_back(std::move(info));
    }
}

void CrashDumpCollector::WriteSummary(const std::vector<DumpFileInfo>& dumps,
                                       int copied,
                                       const fs::path& outputFile)
{
    std::wofstream ofs(outputFile);
    if (!ofs)
        return;

    ofs << L"Crash Dump Collection Summary\n";
    ofs << L"=============================\n\n";
    ofs << L"Total dumps found: " << dumps.size() << L"\n";
    ofs << L"Dumps copied: " << copied << L"\n";
    ofs << L"Filter: max age = " << MAX_DUMP_AGE_DAYS << L" days, max size = 500 MB\n\n";

    if (dumps.empty())
    {
        ofs << L"No crash dump files found.\n";
        return;
    }

    ofs << L"Dumps:\n";
    ofs << L"------\n\n";

    for (const auto& dump : dumps)
    {
        ofs << L"  Source: " << dump.sourcePath.wstring() << L"\n";
        ofs << L"  Label:  " << dump.sourceLabel << L"\n";

        // Format size
        if (dump.fileSize >= 1024 * 1024)
            ofs << L"  Size:   " << (dump.fileSize / (1024 * 1024)) << L" MB\n";
        else if (dump.fileSize >= 1024)
            ofs << L"  Size:   " << (dump.fileSize / 1024) << L" KB\n";
        else
            ofs << L"  Size:   " << dump.fileSize << L" bytes\n";

        ofs << L"\n";
    }
}

bool CrashDumpCollector::Collect(const Config& config,
                                  const fs::path& outputDir,
                                  ProgressCallback cb,
                                  CancelToken cancel)
{
    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    if (cb) cb(Name(), L"Gathering crash dump scan paths...", 0);

    auto scanPaths = GatherScanPaths(config);

    if (scanPaths.empty())
    {
        if (cb) cb(Name(), L"No crash dump paths configured or found", 100);
        Logger::Instance().Log(LogLevel::Info,
            L"[Dumps] No crash dump paths to scan");
        return true;
    }

    // Ensure output directory exists
    std::error_code ec;
    fs::create_directories(outputDir, ec);

    // Scan all paths
    std::vector<DumpFileInfo> allDumps;
    int totalPaths = static_cast<int>(scanPaths.size());

    for (int i = 0; i < totalPaths; ++i)
    {
        if (cancel && cancel->load())
        {
            m_error = L"Cancelled";
            return false;
        }

        const auto& scanPath = scanPaths[i];
        int pct = (i * 40) / totalPaths;
        if (cb) cb(Name(), L"Scanning: " + scanPath, pct);

        // Determine source label
        std::wstring label;
        std::wstring pathLower = scanPath;
        for (auto& ch : pathLower) ch = towlower(ch);

        if (pathLower.find(L"minidump") != std::wstring::npos)
            label = L"minidump";
        else if (pathLower.find(L"crashdumps") != std::wstring::npos)
            label = L"wer";
        else
            label = L"product_" + std::to_wstring(i);

        Logger::Instance().Log(LogLevel::Info,
            L"[Dumps] Scanning: %s (label: %s)", scanPath.c_str(), label.c_str());

        ScanDirectory(fs::path(scanPath), label, allDumps);
    }

    Logger::Instance().Log(LogLevel::Info,
        L"[Dumps] Found %zu dump files matching filters", allDumps.size());

    if (allDumps.empty())
    {
        if (cb) cb(Name(), L"No crash dumps found", 90);
        WriteSummary(allDumps, 0, outputDir / L"dump_summary.txt");
        if (cb) cb(Name(), L"Done", 100);
        return true;
    }

    // Copy dumps to output directory
    int copied = 0;
    int totalDumps = static_cast<int>(allDumps.size());

    for (int i = 0; i < totalDumps; ++i)
    {
        if (cancel && cancel->load())
        {
            m_error = L"Cancelled";
            return false;
        }

        const auto& dump = allDumps[i];
        int pct = 40 + ((i * 50) / totalDumps);
        if (cb) cb(Name(), L"Copying: " + dump.sourcePath.filename().wstring(), pct);

        std::wstring destName = MakeSafeFilename(dump.sourcePath, dump.sourceLabel);
        fs::path destPath = outputDir / destName;

        // Handle potential name collisions by appending index
        if (fs::exists(destPath))
        {
            destName = dump.sourcePath.stem().wstring() + L"_" +
                       dump.sourceLabel + L"_" + std::to_wstring(i) +
                       dump.sourcePath.extension().wstring();
            destPath = outputDir / destName;
        }

        if (fs::copy_file(dump.sourcePath, destPath,
                          fs::copy_options::overwrite_existing, ec))
        {
            ++copied;
            Logger::Instance().Log(LogLevel::Info,
                L"[Dumps] Copied: %s -> %s",
                dump.sourcePath.wstring().c_str(), destPath.wstring().c_str());
        }
        else
        {
            Logger::Instance().Log(LogLevel::Warning,
                L"[Dumps] Failed to copy: %s (%S)",
                dump.sourcePath.wstring().c_str(), ec.message().c_str());
        }
    }

    // Write summary
    if (cb) cb(Name(), L"Writing summary...", 95);
    WriteSummary(allDumps, copied, outputDir / L"dump_summary.txt");

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info,
        L"[Dumps] Collection complete: %d/%d dumps copied", copied, totalDumps);
    return true;
}

} // namespace KTDiag
