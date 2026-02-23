#include <Windows.h>

#include "SystemInfoCollector.h"
#include "../util/Logger.h"
#include "../util/WmiHelper.h"

#include <fstream>
#include <vector>
#include <algorithm>
#include <memory>

// RtlGetVersion from ntdll (avoids GetVersionEx deprecation/lies)
// NTSTATUS and RTL_OSVERSIONINFOW are in <winternl.h> but including it
// causes redefinition conflicts. Use manual typedefs instead.
typedef LONG NTSTATUS;
typedef NTSTATUS(WINAPI* RtlGetVersionFunc)(OSVERSIONINFOW*);

namespace KTDiag
{

static std::wstring ReadRegString(HKEY hKey, const wchar_t* valueName)
{
    DWORD type = 0, dataSize = 0;
    if (RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &dataSize) != ERROR_SUCCESS)
        return L"";
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return L"";

    std::wstring result(dataSize / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(hKey, valueName, nullptr, nullptr,
                         reinterpret_cast<BYTE*>(&result[0]), &dataSize) != ERROR_SUCCESS)
        return L"";

    while (!result.empty() && result.back() == L'\0')
        result.pop_back();

    return result;
}

static DWORD ReadRegDword(HKEY hKey, const wchar_t* valueName)
{
    DWORD value = 0, size = sizeof(DWORD), type = 0;
    if (RegQueryValueExW(hKey, valueName, nullptr, &type,
                         reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
        return value;
    return 0;
}

void SystemInfoCollector::CollectOsInfo(std::wofstream& file)
{
    file << L"--- Operating System ---" << std::endl;

    // Use RtlGetVersion for accurate version (GetVersionEx lies on Win10+)
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll)
    {
        auto pRtlGetVersion = reinterpret_cast<RtlGetVersionFunc>(
            GetProcAddress(hNtdll, "RtlGetVersion"));
        if (pRtlGetVersion)
        {
            OSVERSIONINFOW osvi = {};
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            if (pRtlGetVersion(&osvi) == 0)
            {
                file << L"  OS Version:     " << osvi.dwMajorVersion << L"."
                     << osvi.dwMinorVersion << L" Build " << osvi.dwBuildNumber << std::endl;
            }
        }
    }

    // Detailed info from registry
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        std::wstring productName = ReadRegString(hKey, L"ProductName");
        std::wstring displayVersion = ReadRegString(hKey, L"DisplayVersion");
        std::wstring currentBuild = ReadRegString(hKey, L"CurrentBuild");
        DWORD ubr = ReadRegDword(hKey, L"UBR");
        std::wstring editionId = ReadRegString(hKey, L"EditionID");
        std::wstring installType = ReadRegString(hKey, L"InstallationType");

        // Registry ProductName still says "Windows 10" on Windows 11.
        // Build >= 22000 is Windows 11.
        if (!currentBuild.empty())
        {
            try
            {
                int buildNum = std::stoi(currentBuild);
                if (buildNum >= 22000)
                {
                    size_t pos = productName.find(L"Windows 10");
                    if (pos != std::wstring::npos)
                        productName.replace(pos, 10, L"Windows 11");
                }
            }
            catch (...) {}
        }

        if (!productName.empty())
            file << L"  Product Name:   " << productName << std::endl;
        if (!displayVersion.empty())
            file << L"  Display Ver:    " << displayVersion << std::endl;
        if (!currentBuild.empty())
            file << L"  Build:          " << currentBuild << L"." << ubr << std::endl;
        if (!editionId.empty())
            file << L"  Edition:        " << editionId << std::endl;
        if (!installType.empty())
            file << L"  Install Type:   " << installType << std::endl;

        RegCloseKey(hKey);
    }

    // Architecture
    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    const wchar_t* arch = L"Unknown";
    switch (si.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64: arch = L"x64"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: arch = L"ARM64"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: arch = L"x86"; break;
    }
    file << L"  Architecture:   " << arch << std::endl;

    file << std::endl;
}

void SystemInfoCollector::CollectMachineNames(std::wofstream& file)
{
    file << L"--- Machine Identity ---" << std::endl;

    wchar_t buf[256] = {};
    DWORD len = _countof(buf);

    if (GetComputerNameExW(ComputerNameNetBIOS, buf, &len))
        file << L"  NetBIOS Name:   " << buf << std::endl;

    len = _countof(buf);
    if (GetComputerNameExW(ComputerNameDnsHostname, buf, &len))
        file << L"  DNS Hostname:   " << buf << std::endl;

    len = _countof(buf);
    if (GetComputerNameExW(ComputerNameDnsFullyQualified, buf, &len))
        file << L"  FQDN:           " << buf << std::endl;

    file << std::endl;
}

void SystemInfoCollector::CollectTimeInfo(std::wofstream& file)
{
    file << L"--- Time Information ---" << std::endl;

    // Collection timestamp
    SYSTEMTIME utc, local;
    GetSystemTime(&utc);
    GetLocalTime(&local);

    wchar_t utcBuf[64] = {};
    swprintf_s(utcBuf, L"%04d-%02d-%02d %02d:%02d:%02d UTC",
               utc.wYear, utc.wMonth, utc.wDay,
               utc.wHour, utc.wMinute, utc.wSecond);

    wchar_t localBuf[64] = {};
    swprintf_s(localBuf, L"%04d-%02d-%02d %02d:%02d:%02d",
               local.wYear, local.wMonth, local.wDay,
               local.wHour, local.wMinute, local.wSecond);

    file << L"  UTC Time:       " << utcBuf << std::endl;
    file << L"  Local Time:     " << localBuf << std::endl;

    // Time zone
    TIME_ZONE_INFORMATION tzi = {};
    DWORD tzResult = GetTimeZoneInformation(&tzi);

    file << L"  Time Zone:      " << tzi.StandardName << std::endl;
    file << L"  UTC Offset:     " << -(tzi.Bias) << L" minutes" << std::endl;

    if (tzResult == TIME_ZONE_ID_DAYLIGHT)
        file << L"  DST:            Active (" << tzi.DaylightName << L", bias="
             << tzi.DaylightBias << L" min)" << std::endl;
    else
        file << L"  DST:            Inactive" << std::endl;

    // Uptime
    ULONGLONG uptickMs = GetTickCount64();
    ULONGLONG upDays = uptickMs / (1000ULL * 60 * 60 * 24);
    ULONGLONG upHours = (uptickMs / (1000ULL * 60 * 60)) % 24;
    ULONGLONG upMins = (uptickMs / (1000ULL * 60)) % 60;
    file << L"  Uptime:         " << upDays << L"d " << upHours << L"h " << upMins << L"m"
         << std::endl;

    file << std::endl;
}

void SystemInfoCollector::CollectHardwareInfo(std::wofstream& file)
{
    file << L"--- Hardware ---" << std::endl;

    WmiHelper wmi;
    if (!wmi.Connect(L"ROOT\\CIMV2"))
    {
        file << L"  (WMI connection failed)" << std::endl << std::endl;
        return;
    }

    // Computer system
    std::vector<WmiRow> results;
    if (wmi.Query(L"SELECT Manufacturer, Model, TotalPhysicalMemory FROM Win32_ComputerSystem",
                   {L"Manufacturer", L"Model", L"TotalPhysicalMemory"}, results) && !results.empty())
    {
        const auto& row = results[0];
        auto it = row.find(L"Manufacturer");
        if (it != row.end() && !it->second.empty())
            file << L"  Manufacturer:   " << it->second << std::endl;
        it = row.find(L"Model");
        if (it != row.end() && !it->second.empty())
            file << L"  Model:          " << it->second << std::endl;
        it = row.find(L"TotalPhysicalMemory");
        if (it != row.end() && !it->second.empty())
        {
            try
            {
                ULONGLONG totalMem = std::stoull(it->second);
                wchar_t memBuf[64] = {};
                swprintf_s(memBuf, L"%.2f GB", static_cast<double>(totalMem) / (1ULL << 30));
                file << L"  Total RAM:      " << memBuf << std::endl;
            }
            catch (...) {}
        }
    }

    // Processor
    results.clear();
    if (wmi.Query(L"SELECT Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed FROM Win32_Processor",
                   {L"Name", L"NumberOfCores", L"NumberOfLogicalProcessors", L"MaxClockSpeed"}, results) && !results.empty())
    {
        const auto& row = results[0];
        auto it = row.find(L"Name");
        if (it != row.end())
            file << L"  CPU:            " << it->second << std::endl;
        it = row.find(L"NumberOfCores");
        if (it != row.end())
            file << L"  Cores:          " << it->second << std::endl;
        it = row.find(L"NumberOfLogicalProcessors");
        if (it != row.end())
            file << L"  Logical CPUs:   " << it->second << std::endl;
        it = row.find(L"MaxClockSpeed");
        if (it != row.end())
            file << L"  Max Clock:      " << it->second << L" MHz" << std::endl;
    }

    // BIOS
    results.clear();
    if (wmi.Query(L"SELECT Manufacturer, SMBIOSBIOSVersion, SerialNumber FROM Win32_BIOS",
                   {L"Manufacturer", L"SMBIOSBIOSVersion", L"SerialNumber"}, results) && !results.empty())
    {
        const auto& row = results[0];
        auto it = row.find(L"Manufacturer");
        if (it != row.end())
            file << L"  BIOS Vendor:    " << it->second << std::endl;
        it = row.find(L"SMBIOSBIOSVersion");
        if (it != row.end())
            file << L"  BIOS Version:   " << it->second << std::endl;
        it = row.find(L"SerialNumber");
        if (it != row.end())
            file << L"  Serial Number:  " << it->second << std::endl;
    }

    file << std::endl;
}

bool SystemInfoCollector::CollectInstalledKBs(const std::filesystem::path& outputDir,
                                                CancelToken cancel)
{
    std::filesystem::path outFile = outputDir / L"installed_kbs.txt";
    std::wofstream file(outFile);
    if (!file.is_open()) return true;  // Non-fatal

    file << L"=== Installed Windows Updates (KBs) ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    WmiHelper wmi;
    if (!wmi.Connect(L"ROOT\\CIMV2"))
    {
        file << L"(WMI connection failed)" << std::endl;
        file.close();
        return true;
    }

    std::vector<WmiRow> results;
    if (!wmi.Query(L"SELECT HotFixID, Description, InstalledOn, InstalledBy FROM Win32_QuickFixEngineering",
                    {L"HotFixID", L"Description", L"InstalledOn", L"InstalledBy"}, results))
    {
        file << L"(WMI query failed)" << std::endl;
        file.close();
        return true;
    }

    // Sort by InstalledOn date (descending)
    std::sort(results.begin(), results.end(),
              [](const WmiRow& a, const WmiRow& b) {
                  auto itA = a.find(L"InstalledOn");
                  auto itB = b.find(L"InstalledOn");
                  std::wstring dateA = (itA != a.end()) ? itA->second : L"";
                  std::wstring dateB = (itB != b.end()) ? itB->second : L"";
                  return dateA > dateB;  // Descending
              });

    for (const auto& row : results)
    {
        if (cancel && cancel->load()) break;

        auto it = row.find(L"HotFixID");
        if (it == row.end()) continue;
        file << it->second;

        it = row.find(L"Description");
        if (it != row.end() && !it->second.empty())
            file << L"  |  " << it->second;

        it = row.find(L"InstalledOn");
        if (it != row.end() && !it->second.empty())
            file << L"  |  Installed: " << it->second;

        it = row.find(L"InstalledBy");
        if (it != row.end() && !it->second.empty())
            file << L"  |  By: " << it->second;

        file << std::endl;
    }

    file << std::endl << L"Total KBs: " << results.size() << std::endl;
    file.close();

    Logger::Instance().Log(LogLevel::Info, L"[SystemInfo] Found %zu installed KBs", results.size());
    return true;
}

bool SystemInfoCollector::Collect(const Config& config,
                                    const std::filesystem::path& outputDir,
                                    ProgressCallback cb,
                                    CancelToken cancel)
{
    UNREFERENCED_PARAMETER(config);
    if (cb) cb(Name(), L"Collecting system information...", 0);

    std::filesystem::path outFile = outputDir / L"system_info.txt";
    std::wofstream file(outFile);
    if (!file.is_open())
    {
        m_error = L"Failed to create output file: " + outFile.wstring();
        return false;
    }

    file << L"=== System Information ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    if (cb) cb(Name(), L"Collecting OS info...", 10);
    CollectOsInfo(file);

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Collecting machine identity...", 25);
    CollectMachineNames(file);

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Collecting time info...", 40);
    CollectTimeInfo(file);

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Querying hardware via WMI...", 55);
    CollectHardwareInfo(file);

    file.close();

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Querying installed KBs...", 75);
    CollectInstalledKBs(outputDir, cancel);

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[SystemInfo] System information collected");
    return true;
}

} // namespace KTDiag
