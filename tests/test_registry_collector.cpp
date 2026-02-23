#include <gtest/gtest.h>
#include "collectors/RegistryCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class RegistryCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_Registry";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(RegistryCollectorTest, NameAndId)
{
    KTDiag::RegistryCollector collector;
    EXPECT_EQ(collector.Name(), L"Registry Keys");
    EXPECT_EQ(collector.Id(), L"registry");
}

TEST_F(RegistryCollectorTest, EmptyConfigSucceeds)
{
    KTDiag::Config config; // No registry keys configured
    KTDiag::RegistryCollector collector;

    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);
}

TEST_F(RegistryCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::RegistryCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true);
    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}
