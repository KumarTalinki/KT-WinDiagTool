#include <Windows.h>

#include "DiskSpaceCollector.h"
#include "../util/Logger.h"

#include <fstream>
#include <vector>
#include <cstdio>

namespace KTDiag
{

std::wstring DiskSpaceCollector::FormatBytes(ULONGLONG bytes)
{
    wchar_t buf[64] = {};
    if (bytes >= (1ULL << 40))
        swprintf_s(buf, L"%.2f TB", static_cast<double>(bytes) / (1ULL << 40));
    else if (bytes >= (1ULL << 30))
        swprintf_s(buf, L"%.2f GB", static_cast<double>(bytes) / (1ULL << 30));
    else if (bytes >= (1ULL << 20))
        swprintf_s(buf, L"%.2f MB", static_cast<double>(bytes) / (1ULL << 20));
    else
        swprintf_s(buf, L"%llu KB", bytes / 1024);
    return buf;
}

const wchar_t* DiskSpaceCollector::DriveTypeToString(UINT driveType)
{
    switch (driveType)
    {
    case DRIVE_REMOVABLE: return L"Removable";
    case DRIVE_FIXED:     return L"Fixed";
    case DRIVE_REMOTE:    return L"Network";
    case DRIVE_CDROM:     return L"CD-ROM";
    case DRIVE_RAMDISK:   return L"RAM Disk";
    default:              return L"Unknown";
    }
}

bool DiskSpaceCollector::Collect(const Config& config,
                                  const std::filesystem::path& outputDir,
                                  ProgressCallback cb,
                                  CancelToken cancel)
{
    if (cb) cb(Name(), L"Enumerating drives...", 0);

    std::filesystem::path outFile = outputDir / L"disk_space.txt";
    std::wofstream file(outFile);
    if (!file.is_open())
    {
        m_error = L"Failed to create output file: " + outFile.wstring();
        return false;
    }

    file << L"=== Disk Space ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    // Get logical drive strings
    wchar_t drives[512] = {};
    DWORD len = GetLogicalDriveStringsW(_countof(drives) - 1, drives);
    if (len == 0)
    {
        file << L"Failed to enumerate drives." << std::endl;
        file.close();
        m_error = L"GetLogicalDriveStrings failed";
        return false;
    }

    // Resolve config log/dump paths to drive letters for flagging
    std::vector<wchar_t> productDrives;
    for (const auto& logPath : config.GetLogPaths())
    {
        std::wstring expanded = Config::ExpandEnvironmentPath(logPath);
        if (expanded.size() >= 2 && expanded[1] == L':')
            productDrives.push_back(towupper(expanded[0]));
    }
    for (const auto& dumpPath : config.GetCrashDumpPaths())
    {
        std::wstring expanded = Config::ExpandEnvironmentPath(dumpPath);
        if (expanded.size() >= 2 && expanded[1] == L':')
            productDrives.push_back(towupper(expanded[0]));
    }

    // Get system drive letter
    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    wchar_t systemDrive = (sysDir[0] != L'\0') ? towupper(sysDir[0]) : L'C';

    int driveCount = 0;
    const wchar_t* ptr = drives;
    while (*ptr)
    {
        if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }

        std::wstring root = ptr;
        ptr += wcslen(ptr) + 1;

        UINT driveType = GetDriveTypeW(root.c_str());

        // Skip non-fixed/removable drives for detailed info
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE && driveType != DRIVE_RAMDISK)
        {
            file << root << L"  " << DriveTypeToString(driveType) << L"  (skipped)" << std::endl;
            ++driveCount;
            continue;
        }

        // Volume info
        wchar_t volumeName[MAX_PATH] = {};
        wchar_t fsName[64] = {};
        DWORD serialNumber = 0;
        GetVolumeInformationW(root.c_str(), volumeName, MAX_PATH,
                              &serialNumber, nullptr, nullptr, fsName, _countof(fsName));

        // Free space
        ULARGE_INTEGER freeBytesAvailable = {}, totalBytes = {}, totalFreeBytes = {};
        GetDiskFreeSpaceExW(root.c_str(), &freeBytesAvailable, &totalBytes, &totalFreeBytes);

        wchar_t driveLetter = towupper(root[0]);

        file << L"--- " << root;
        if (volumeName[0] != L'\0')
            file << L" [" << volumeName << L"]";
        file << L" ---" << std::endl;

        file << L"  Type:           " << DriveTypeToString(driveType) << std::endl;
        file << L"  File System:    " << fsName << std::endl;
        file << L"  Total:          " << FormatBytes(totalBytes.QuadPart) << std::endl;
        file << L"  Free:           " << FormatBytes(totalFreeBytes.QuadPart) << std::endl;
        file << L"  Available:      " << FormatBytes(freeBytesAvailable.QuadPart) << std::endl;

        if (totalBytes.QuadPart > 0)
        {
            double pctFree = 100.0 * totalFreeBytes.QuadPart / totalBytes.QuadPart;
            wchar_t pctBuf[32] = {};
            swprintf_s(pctBuf, L"%.1f%%", pctFree);
            file << L"  Free %%:         " << pctBuf << std::endl;
        }

        // Warnings
        ULONGLONG freeGB = totalFreeBytes.QuadPart / (1ULL << 30);
        ULONGLONG freeMB = totalFreeBytes.QuadPart / (1ULL << 20);

        if (driveLetter == systemDrive && freeGB < 1)
        {
            file << L"  ** WARNING: System drive has less than 1 GB free! **" << std::endl;
        }

        // Check if this drive holds product logs/dumps
        for (wchar_t pd : productDrives)
        {
            if (driveLetter == pd && freeMB < 500)
            {
                file << L"  ** WARNING: Product log/dump drive has less than 500 MB free! **"
                     << std::endl;
                break;
            }
        }

        file << std::endl;
        ++driveCount;
    }

    file << L"========================================" << std::endl;
    file << L"Total drives: " << driveCount << std::endl;
    file.close();

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[DiskSpace] Enumerated %d drives", driveCount);
    return true;
}

} // namespace KTDiag
