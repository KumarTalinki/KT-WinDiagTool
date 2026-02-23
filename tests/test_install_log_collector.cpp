#include <gtest/gtest.h>
#include "collectors/InstallLogCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class InstallLogCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_InstallLogs";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(InstallLogCollectorTest, NameAndId)
{
    KTDiag::InstallLogCollector collector;
    EXPECT_EQ(collector.Name(), L"Installation Logs");
    EXPECT_EQ(collector.Id(), L"installlogs");
}

TEST_F(InstallLogCollectorTest, EmptyConfigSucceeds)
{
    KTDiag::Config config; // No install_log_paths configured
    KTDiag::InstallLogCollector collector;

    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);
}

TEST_F(InstallLogCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::InstallLogCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true);
    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}
