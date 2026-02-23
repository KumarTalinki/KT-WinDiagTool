#include <gtest/gtest.h>
#include "collectors/ProductLogCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class ProductLogCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_ProductLogs";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(ProductLogCollectorTest, NameAndId)
{
    KTDiag::ProductLogCollector collector;
    EXPECT_EQ(collector.Name(), L"Product Logs");
    EXPECT_EQ(collector.Id(), L"productlogs");
}

TEST_F(ProductLogCollectorTest, EmptyConfigSucceeds)
{
    KTDiag::Config config; // No log_paths configured
    KTDiag::ProductLogCollector collector;

    // Should succeed gracefully even with no paths
    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);
}

TEST_F(ProductLogCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::ProductLogCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true);
    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}
