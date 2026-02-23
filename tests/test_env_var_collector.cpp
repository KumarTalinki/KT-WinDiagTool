#include <gtest/gtest.h>
#include "collectors/EnvVarCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class EnvVarCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_EnvVar";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(EnvVarCollectorTest, NameAndId)
{
    KTDiag::EnvVarCollector collector;
    EXPECT_EQ(collector.Name(), L"Environment Variables");
    EXPECT_EQ(collector.Id(), L"env");
}

TEST_F(EnvVarCollectorTest, CollectProducesOutput)
{
    KTDiag::Config config;
    KTDiag::EnvVarCollector collector;

    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);
    EXPECT_TRUE(collector.GetError().empty());

    // Should produce at least one file in the output directory
    bool hasFiles = false;
    for (const auto& entry : fs::directory_iterator(m_outputDir))
    {
        if (entry.is_regular_file())
        {
            hasFiles = true;
            EXPECT_GT(entry.file_size(), 0u);
        }
    }
    EXPECT_TRUE(hasFiles);
}

TEST_F(EnvVarCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::EnvVarCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true);
    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}
