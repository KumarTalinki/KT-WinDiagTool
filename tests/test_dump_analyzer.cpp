#include <gtest/gtest.h>
#include "analysis/DumpAnalyzer.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class DumpAnalyzerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_DumpAnalyzer";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(DumpAnalyzerTest, AnalyzeNonexistentFile)
{
    KTDiag::DumpAnalyzer analyzer;
    auto result = analyzer.Analyze(L"C:\\nonexistent\\file.dmp");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(DumpAnalyzerTest, AnalyzeInvalidFile)
{
    // Create a file that isn't a valid minidump
    fs::path fakeFile = m_outputDir / L"fake.dmp";
    {
        std::ofstream f(fakeFile, std::ios::binary);
        f << "This is not a valid dump file";
    }

    KTDiag::DumpAnalyzer analyzer;
    auto result = analyzer.Analyze(fakeFile);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(DumpAnalyzerTest, WriteReportCreatesFile)
{
    // Create a mock result and write a report
    KTDiag::DumpAnalysisResult result;
    result.dumpPath = L"test.dmp";
    result.success = true;
    result.exceptionCode = L"0xC0000005";
    result.faultingModule = L"test.dll";
    result.callStack.push_back(L"test!Function+0x10");
    result.callStack.push_back(L"ntdll!RtlUserThreadStart+0x20");

    fs::path reportFile = m_outputDir / L"test_report.txt";
    KTDiag::DumpAnalyzer::WriteReport(result, reportFile);

    EXPECT_TRUE(fs::exists(reportFile));
    EXPECT_GT(fs::file_size(reportFile), 0u);
}

TEST_F(DumpAnalyzerTest, WriteReportForFailedAnalysis)
{
    KTDiag::DumpAnalysisResult result;
    result.dumpPath = L"bad.dmp";
    result.success = false;
    result.error = L"File not found";

    fs::path reportFile = m_outputDir / L"bad_report.txt";
    KTDiag::DumpAnalyzer::WriteReport(result, reportFile);

    EXPECT_TRUE(fs::exists(reportFile));
}
