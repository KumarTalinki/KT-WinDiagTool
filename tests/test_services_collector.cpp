#include <gtest/gtest.h>
#include "collectors/ServicesCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class ServicesCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_Services";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(ServicesCollectorTest, NameAndId)
{
    KTDiag::ServicesCollector collector;
    EXPECT_EQ(collector.Name(), L"Windows Services");
    EXPECT_EQ(collector.Id(), L"services");
}

TEST_F(ServicesCollectorTest, CollectProducesOutput)
{
    KTDiag::Config config;
    KTDiag::ServicesCollector collector;

    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);

    // Every Windows machine has services
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

TEST_F(ServicesCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::ServicesCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true);
    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}
