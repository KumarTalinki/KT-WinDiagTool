#include <gtest/gtest.h>
#include "collectors/CrashDumpCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class CrashDumpCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_CrashDump";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(CrashDumpCollectorTest, NameAndId)
{
    KTDiag::CrashDumpCollector collector;
    EXPECT_EQ(collector.Name(), L"Crash Dump Collection");
    EXPECT_EQ(collector.Id(), L"dumps");
}

TEST_F(CrashDumpCollectorTest, EmptyConfigSucceeds)
{
    KTDiag::Config config; // No crash dump paths configured
    KTDiag::CrashDumpCollector collector;

    // Should succeed even with no dump paths (scans default system locations)
    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);
}

TEST_F(CrashDumpCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::CrashDumpCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true);
    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}
