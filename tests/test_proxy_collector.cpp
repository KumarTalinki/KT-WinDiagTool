#include <gtest/gtest.h>
#include "collectors/ProxySettingsCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class ProxySettingsCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_Proxy";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(ProxySettingsCollectorTest, NameAndId)
{
    KTDiag::ProxySettingsCollector collector;
    EXPECT_EQ(collector.Name(), L"Proxy Settings");
    EXPECT_EQ(collector.Id(), L"proxy");
}

TEST_F(ProxySettingsCollectorTest, CollectSucceeds)
{
    KTDiag::Config config;
    KTDiag::ProxySettingsCollector collector;

    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);
}

TEST_F(ProxySettingsCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::ProxySettingsCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true);
    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}
