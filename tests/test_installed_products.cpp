#include <gtest/gtest.h>
#include "collectors/InstalledProductsCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class InstalledProductsCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_Products";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(InstalledProductsCollectorTest, NameAndId)
{
    KTDiag::InstalledProductsCollector collector;
    EXPECT_EQ(collector.Name(), L"Installed Products");
    EXPECT_EQ(collector.Id(), L"installed");
}

TEST_F(InstalledProductsCollectorTest, CollectProducesOutput)
{
    KTDiag::Config config;
    KTDiag::InstalledProductsCollector collector;

    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);

    // Should produce output — any Windows machine has installed software
    bool hasFiles = false;
    for (const auto& entry : fs::directory_iterator(m_outputDir))
    {
        if (entry.is_regular_file())
            hasFiles = true;
    }
    EXPECT_TRUE(hasFiles);
}

TEST_F(InstalledProductsCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::InstalledProductsCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true);
    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}
