#include <Windows.h>

#include "WerCollector.h"
#include "../util/Logger.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

namespace KTDiag
{

WerCollector::WerReport WerCollector::ParseReportWer(const std::filesystem::path& reportFile)
{
    WerReport report;

    std::wifstream file(reportFile);
    if (!file.is_open())
        return report;

    std::wstring line;
    while (std::getline(file, line))
    {
        // Report.wer format: Key=Value
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;

        std::wstring key = line.substr(0, eq);
        std::wstring value = line.substr(eq + 1);

        // Trim trailing \r if present
        if (!value.empty() && value.back() == L'\r')
            value.pop_back();

        if (key == L"EventType")
            report.eventType = value;
        else if (key == L"AppName" || key == L"P1")
            report.faultingApp = value;
        else if (key == L"ModName" || key == L"P3")
            report.faultingModule = value;
        else if (key == L"ExceptionCode" || key == L"P6")
            report.exceptionCode = value;
        else if (key == L"ReportIdentifier")
            report.timestamp = value;
    }

    return report;
}

void WerCollector::ScanWerDirectory(const std::filesystem::path& werDir,
                                     const std::wstring& productName,
                                     std::vector<WerReport>& reports,
                                     CancelToken cancel)
{
    std::error_code ec;
    if (!std::filesystem::exists(werDir, ec) || !std::filesystem::is_directory(werDir, ec))
        return;

    for (const auto& entry : std::filesystem::directory_iterator(werDir, ec))
    {
        if (cancel && cancel->load()) break;
        if (!entry.is_directory()) continue;

        std::filesystem::path reportWer = entry.path() / L"Report.wer";
        if (!std::filesystem::exists(reportWer, ec))
            continue;

        WerReport report = ParseReportWer(reportWer);
        report.folderName = entry.path().filename().wstring();

        // Check if this report matches the product
        if (!productName.empty())
        {
            std::wstring appLower = report.faultingApp;
            std::wstring prodLower = productName;
            std::transform(appLower.begin(), appLower.end(), appLower.begin(), towlower);
            std::transform(prodLower.begin(), prodLower.end(), prodLower.begin(), towlower);

            if (appLower.find(prodLower) != std::wstring::npos)
                report.matchesProduct = true;

            // Also check folder name
            std::wstring folderLower = report.folderName;
            std::transform(folderLower.begin(), folderLower.end(), folderLower.begin(), towlower);
            if (folderLower.find(prodLower) != std::wstring::npos)
                report.matchesProduct = true;
        }

        reports.push_back(std::move(report));
    }
}

void WerCollector::CollectWerPolicy(std::wofstream& file)
{
    file << L"--- WER Policy Settings ---" << std::endl;

    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting",
        0, KEY_READ, &hKey);

    if (rc != ERROR_SUCCESS)
    {
        file << L"  (unable to read WER policy, error=" << rc << L")" << std::endl << std::endl;
        return;
    }

    auto readDword = [&](const wchar_t* name, DWORD defaultVal) -> DWORD {
        DWORD value = 0, size = sizeof(DWORD), type = 0;
        if (RegQueryValueExW(hKey, name, nullptr, &type,
                             reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
            type == REG_DWORD)
            return value;
        return defaultVal;
    };

    DWORD disabled = readDword(L"Disabled", 0);
    DWORD dontShowUI = readDword(L"DontShowUI", 0);
    DWORD consent = readDword(L"Consent", 0);

    file << L"  WER Disabled:      " << (disabled ? L"Yes" : L"No") << std::endl;
    file << L"  Don't Show UI:     " << (dontShowUI ? L"Yes" : L"No") << std::endl;
    file << L"  Consent Level:     " << consent << std::endl;

    // Check LocalDumps configuration
    HKEY hDumps = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps",
        0, KEY_READ, &hDumps) == ERROR_SUCCESS)
    {
        file << L"  Local Dumps:       Configured" << std::endl;

        wchar_t dumpFolder[MAX_PATH] = {};
        DWORD folderSize = sizeof(dumpFolder);
        if (RegQueryValueExW(hDumps, L"DumpFolder", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(dumpFolder), &folderSize) == ERROR_SUCCESS)
        {
            file << L"  Dump Folder:       " << dumpFolder << std::endl;
        }

        DWORD dumpCount = 0, countSize = sizeof(dumpCount);
        if (RegQueryValueExW(hDumps, L"DumpCount", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&dumpCount), &countSize) == ERROR_SUCCESS)
        {
            file << L"  Max Dump Count:    " << dumpCount << std::endl;
        }

        DWORD dumpType = 0, typeSize = sizeof(dumpType);
        if (RegQueryValueExW(hDumps, L"DumpType", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&dumpType), &typeSize) == ERROR_SUCCESS)
        {
            const wchar_t* typeStr = L"Custom";
            switch (dumpType)
            {
            case 0: typeStr = L"Custom"; break;
            case 1: typeStr = L"Mini"; break;
            case 2: typeStr = L"Full"; break;
            }
            file << L"  Dump Type:         " << typeStr << std::endl;
        }

        RegCloseKey(hDumps);
    }
    else
    {
        file << L"  Local Dumps:       Not configured (default)" << std::endl;
    }

    RegCloseKey(hKey);
    file << std::endl;
}

bool WerCollector::Collect(const Config& config,
                            const std::filesystem::path& outputDir,
                            ProgressCallback cb,
                            CancelToken cancel)
{
    if (cb) cb(Name(), L"Scanning WER reports...", 0);

    // Expand WER paths
    wchar_t progData[MAX_PATH] = {};
    GetEnvironmentVariableW(L"ProgramData", progData, MAX_PATH);
    std::filesystem::path werBase = std::filesystem::path(progData) /
        L"Microsoft" / L"Windows" / L"WER";

    std::filesystem::path archiveDir = werBase / L"ReportArchive";
    std::filesystem::path queueDir = werBase / L"ReportQueue";

    std::vector<WerReport> allReports;
    std::wstring productName = config.GetProductName();

    if (cb) cb(Name(), L"Scanning ReportArchive...", 10);
    ScanWerDirectory(archiveDir, productName, allReports, cancel);

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Scanning ReportQueue...", 30);
    ScanWerDirectory(queueDir, productName, allReports, cancel);

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }

    // Write summary
    if (cb) cb(Name(), L"Writing WER summary...", 50);

    std::filesystem::path summaryFile = outputDir / L"wer_summary.txt";
    std::wofstream file(summaryFile);
    if (!file.is_open())
    {
        m_error = L"Failed to create output file: " + summaryFile.wstring();
        return false;
    }

    file << L"=== Windows Error Reporting Summary ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    CollectWerPolicy(file);

    file << L"--- WER Reports ---" << std::endl;
    file << L"Total reports found: " << allReports.size() << std::endl << std::endl;

    int productCount = 0;
    for (const auto& report : allReports)
    {
        if (report.matchesProduct)
        {
            file << L"  [PRODUCT MATCH] ";
            ++productCount;
        }
        else
        {
            file << L"  ";
        }
        file << report.folderName;
        if (!report.eventType.empty())
            file << L"  |  Event: " << report.eventType;
        if (!report.faultingApp.empty())
            file << L"  |  App: " << report.faultingApp;
        if (!report.faultingModule.empty())
            file << L"  |  Module: " << report.faultingModule;
        if (!report.exceptionCode.empty())
            file << L"  |  Exception: " << report.exceptionCode;
        file << std::endl;
    }

    file << std::endl << L"Product-related reports: " << productCount << std::endl;
    file.close();

    // Copy product-matching WER report folders
    if (productCount > 0)
    {
        if (cb) cb(Name(), L"Copying product WER reports...", 70);

        std::filesystem::path werOutDir = outputDir / L"wer_reports";
        std::error_code ec;
        std::filesystem::create_directories(werOutDir, ec);

        for (const auto& report : allReports)
        {
            if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
            if (!report.matchesProduct) continue;

            // Find the source folder in archive or queue
            std::filesystem::path srcArchive = archiveDir / report.folderName;
            std::filesystem::path srcQueue = queueDir / report.folderName;
            std::filesystem::path src = std::filesystem::exists(srcArchive, ec) ?
                                        srcArchive : srcQueue;

            if (std::filesystem::exists(src, ec))
            {
                std::filesystem::path dest = werOutDir / report.folderName;
                std::filesystem::copy(src, dest,
                    std::filesystem::copy_options::recursive |
                    std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    }

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info,
        L"[WER] Found %zu reports (%d product-related)",
        allReports.size(), productCount);
    return true;
}

} // namespace KTDiag
