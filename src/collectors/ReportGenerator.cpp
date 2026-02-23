#include "ReportGenerator.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

namespace
{

std::wstring GetTimestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wostringstream oss;
    oss << st.wYear << L"-"
        << std::setfill(L'0') << std::setw(2) << st.wMonth << L"-"
        << std::setw(2) << st.wDay << L" "
        << std::setw(2) << st.wHour << L":"
        << std::setw(2) << st.wMinute << L":"
        << std::setw(2) << st.wSecond;
    return oss.str();
}

std::wstring GetMachineName()
{
    wchar_t buf[256] = {};
    DWORD size = 256;
    if (GetComputerNameExW(ComputerNameDnsFullyQualified, buf, &size))
        return std::wstring(buf);
    size = 256;
    if (GetComputerNameW(buf, &size))
        return std::wstring(buf);
    return L"(unknown)";
}

std::wstring GetCurrentUser()
{
    wchar_t buf[256] = {};
    DWORD size = 256;
    if (GetUserNameW(buf, &size))
        return std::wstring(buf);
    return L"(unknown)";
}

std::wstring HtmlEscape(const std::wstring& text)
{
    std::wstring result;
    result.reserve(text.size());
    for (wchar_t ch : text)
    {
        switch (ch)
        {
        case L'<': result += L"&lt;"; break;
        case L'>': result += L"&gt;"; break;
        case L'&': result += L"&amp;"; break;
        case L'"': result += L"&quot;"; break;
        default:   result += ch; break;
        }
    }
    return result;
}

} // anonymous namespace

namespace KTDiag
{

void ReportGenerator::WriteHtmlHeader(std::wofstream& ofs, const std::wstring& productName)
{
    ofs << L"<!DOCTYPE html>\n"
        << L"<html lang=\"en\">\n"
        << L"<head>\n"
        << L"<meta charset=\"UTF-8\">\n"
        << L"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        << L"<title>KT-WinDiagTool Report - " << HtmlEscape(productName) << L"</title>\n"
        << L"<style>\n"
        << L"  body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 20px; "
        << L"background: #f5f5f5; color: #333; }\n"
        << L"  h1 { color: #1a5276; border-bottom: 2px solid #1a5276; padding-bottom: 8px; }\n"
        << L"  h2 { color: #2c3e50; margin-top: 30px; }\n"
        << L"  .info-grid { display: grid; grid-template-columns: 180px auto; gap: 4px 16px; "
        << L"margin: 10px 0; }\n"
        << L"  .info-label { font-weight: 600; color: #555; }\n"
        << L"  table { border-collapse: collapse; width: 100%; margin: 10px 0; }\n"
        << L"  th, td { border: 1px solid #ddd; padding: 8px 12px; text-align: left; }\n"
        << L"  th { background: #1a5276; color: white; }\n"
        << L"  tr:nth-child(even) { background: #eaf2f8; }\n"
        << L"  .ok { color: #27ae60; font-weight: 600; }\n"
        << L"  .fail { color: #e74c3c; font-weight: 600; }\n"
        << L"  .stats { background: #fff; border: 1px solid #ddd; padding: 12px 20px; "
        << L"border-radius: 4px; display: inline-block; margin: 10px 0; }\n"
        << L"  .stats span { margin-right: 24px; }\n"
        << L"  .artifact-list { list-style: none; padding: 0; }\n"
        << L"  .artifact-list li { padding: 3px 0; font-family: 'Consolas', monospace; font-size: 13px; }\n"
        << L"  .dir { font-weight: 600; color: #2c3e50; }\n"
        << L"  .file { color: #555; }\n"
        << L"  footer { margin-top: 40px; padding-top: 10px; border-top: 1px solid #ccc; "
        << L"color: #888; font-size: 12px; }\n"
        << L"</style>\n"
        << L"</head>\n"
        << L"<body>\n"
        << L"<h1>KT-WinDiagTool - Diagnostics Report</h1>\n";
}

void ReportGenerator::WriteInfoSection(std::wofstream& ofs, const Config& config)
{
    ofs << L"<h2>Collection Info</h2>\n"
        << L"<div class=\"info-grid\">\n"
        << L"  <span class=\"info-label\">Product:</span><span>" << HtmlEscape(config.GetProductName()) << L"</span>\n"
        << L"  <span class=\"info-label\">Machine:</span><span>" << HtmlEscape(GetMachineName()) << L"</span>\n"
        << L"  <span class=\"info-label\">User:</span><span>" << HtmlEscape(GetCurrentUser()) << L"</span>\n"
        << L"  <span class=\"info-label\">Timestamp:</span><span>" << GetTimestamp() << L"</span>\n"
        << L"  <span class=\"info-label\">Tool Version:</span><span>KT-WinDiagTool v1.0.0</span>\n"
        << L"</div>\n";
}

void ReportGenerator::WriteResultsTable(std::wofstream& ofs,
                                         const std::vector<CollectorResult>& results)
{
    int passed = 0, failed = 0;
    double totalTime = 0.0;

    for (const auto& r : results)
    {
        if (r.success) ++passed; else ++failed;
        totalTime += r.elapsedSeconds;
    }

    ofs << L"<h2>Collection Results</h2>\n";

    // Statistics bar
    ofs << L"<div class=\"stats\">\n"
        << L"  <span>Total: <b>" << results.size() << L"</b></span>\n"
        << L"  <span class=\"ok\">Passed: " << passed << L"</span>\n"
        << L"  <span class=\"fail\">Failed: " << failed << L"</span>\n"
        << L"  <span>Time: " << std::fixed << std::setprecision(1) << totalTime << L"s</span>\n"
        << L"</div>\n";

    // Results table
    ofs << L"<table>\n"
        << L"<tr><th>Collector</th><th>Status</th><th>Duration</th><th>Error</th></tr>\n";

    for (const auto& r : results)
    {
        ofs << L"<tr>\n"
            << L"  <td>" << HtmlEscape(r.collectorName) << L"</td>\n"
            << L"  <td class=\"" << (r.success ? L"ok" : L"fail") << L"\">"
            << (r.success ? L"OK" : L"FAIL") << L"</td>\n"
            << L"  <td>" << std::fixed << std::setprecision(1) << r.elapsedSeconds << L"s</td>\n"
            << L"  <td>" << (r.errorMessage.empty() ? L"&mdash;" : HtmlEscape(r.errorMessage)) << L"</td>\n"
            << L"</tr>\n";
    }

    ofs << L"</table>\n";
}

void ReportGenerator::WriteArtifactsList(std::wofstream& ofs,
                                          const fs::path& outputDir)
{
    ofs << L"<h2>Collected Artifacts</h2>\n"
        << L"<ul class=\"artifact-list\">\n";

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(outputDir, ec))
    {
        fs::path relPath = entry.path().filename();
        std::wstring name = relPath.wstring();

        if (entry.is_directory())
        {
            ofs << L"<li class=\"dir\">" << HtmlEscape(name) << L"/</li>\n";

            // List files in subdirectory (one level deep)
            for (const auto& subEntry : fs::directory_iterator(entry.path(), ec))
            {
                if (subEntry.is_regular_file())
                {
                    auto subSize = subEntry.file_size(ec);
                    std::wstring sizeStr;
                    if (subSize >= 1024 * 1024)
                        sizeStr = std::to_wstring(subSize / (1024 * 1024)) + L" MB";
                    else if (subSize >= 1024)
                        sizeStr = std::to_wstring(subSize / 1024) + L" KB";
                    else
                        sizeStr = std::to_wstring(subSize) + L" B";

                    ofs << L"<li class=\"file\">&nbsp;&nbsp;&nbsp;&nbsp;"
                        << HtmlEscape(name + L"/" + subEntry.path().filename().wstring())
                        << L" <span style=\"color:#999\">(" << sizeStr << L")</span></li>\n";
                }
            }
        }
        else if (entry.is_regular_file())
        {
            auto fsize = entry.file_size(ec);
            std::wstring sizeStr;
            if (fsize >= 1024 * 1024)
                sizeStr = std::to_wstring(fsize / (1024 * 1024)) + L" MB";
            else if (fsize >= 1024)
                sizeStr = std::to_wstring(fsize / 1024) + L" KB";
            else
                sizeStr = std::to_wstring(fsize) + L" B";

            ofs << L"<li class=\"file\">" << HtmlEscape(name)
                << L" <span style=\"color:#999\">(" << sizeStr << L")</span></li>\n";
        }
    }

    ofs << L"</ul>\n";
}

void ReportGenerator::WriteHtmlFooter(std::wofstream& ofs)
{
    ofs << L"<footer>\n"
        << L"Generated by KT-WinDiagTool v1.0.0 | " << GetTimestamp() << L"\n"
        << L"</footer>\n"
        << L"</body>\n"
        << L"</html>\n";
}

bool ReportGenerator::Generate(const fs::path& outputDir,
                                const Config& config,
                                const std::vector<CollectorResult>& results)
{
    fs::path reportPath = outputDir / L"summary.html";

    std::wofstream ofs(reportPath);
    if (!ofs)
    {
        m_error = L"Failed to create summary.html";
        return false;
    }

    WriteHtmlHeader(ofs, config.GetProductName());
    WriteInfoSection(ofs, config);
    WriteResultsTable(ofs, results);
    WriteArtifactsList(ofs, outputDir);
    WriteHtmlFooter(ofs);

    ofs.close();

    Logger::Instance().Log(LogLevel::Info,
        L"[Report] Summary report generated: %s", reportPath.wstring().c_str());
    return true;
}

} // namespace KTDiag
