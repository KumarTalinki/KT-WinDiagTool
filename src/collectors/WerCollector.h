#pragma once

#include "ICollector.h"

namespace KTDiag
{

class WerCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Windows Error Reporting"; }
    std::wstring Id() const override { return L"wer"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    struct WerReport
    {
        std::wstring folderName;
        std::wstring eventType;
        std::wstring faultingApp;
        std::wstring faultingModule;
        std::wstring exceptionCode;
        std::wstring timestamp;
        bool matchesProduct = false;
    };

    // Scan a WER directory (ReportArchive or ReportQueue) for reports
    void ScanWerDirectory(const std::filesystem::path& werDir,
                          const std::wstring& productName,
                          std::vector<WerReport>& reports,
                          CancelToken cancel);

    // Parse a Report.wer file for key fields
    static WerReport ParseReportWer(const std::filesystem::path& reportFile);

    // Collect WER policy settings from registry
    void CollectWerPolicy(std::wofstream& file);

    std::wstring m_error;
};

} // namespace KTDiag
