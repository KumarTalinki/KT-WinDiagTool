#include <gtest/gtest.h>
#include "analysis/WinDbgAutomation.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class WinDbgAutomationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_WinDbg";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(WinDbgAutomationTest, FindCdbReturnsPathOrEmpty)
{
    fs::path cdbPath = KTDiag::WinDbgAutomation::FindCdb();
    // Either found or not — just verify it doesn't crash
    if (!cdbPath.empty())
    {
        EXPECT_TRUE(fs::exists(cdbPath));
    }
}

TEST_F(WinDbgAutomationTest, WriteNotFoundReportCreatesFile)
{
    fs::path reportFile = m_outputDir / L"windbg_not_found.txt";
    KTDiag::WinDbgAutomation::WriteNotFoundReport(reportFile);

    EXPECT_TRUE(fs::exists(reportFile));
    EXPECT_GT(fs::file_size(reportFile), 0u);
}

TEST_F(WinDbgAutomationTest, AnalyzeNonexistentDumpFails)
{
    KTDiag::WinDbgAutomation windbg;
    fs::path outputFile = m_outputDir / L"analysis_output.txt";

    bool result = windbg.Analyze(L"C:\\nonexistent\\dump.dmp", outputFile, 5000);
    EXPECT_FALSE(result);
    EXPECT_FALSE(windbg.GetError().empty());
}
