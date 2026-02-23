#include <gtest/gtest.h>
#include "collectors/EventLogCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Extends the tests in test_collectors_phase3.cpp with additional coverage

class EventLogCollectorExtTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_EventLogExt";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    // Create a minimal config with event log channels
    KTDiag::Config CreateConfigWithChannels()
    {
        KTDiag::Config config;
        // Use a temp JSON file with channels specified
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        fs::path configFile = fs::path(tempPath) / L"test_eventlog_config.json";
        {
            std::ofstream f(configFile);
            f << R"({
                "product_name": "TestProduct",
                "event_log_channels": ["Application", "System"]
            })";
        }
        config.LoadFromFile(configFile.wstring());
        fs::remove(configFile);
        return config;
    }

    fs::path m_outputDir;
};

TEST_F(EventLogCollectorExtTest, CollectWithChannelsProducesOutput)
{
    auto config = CreateConfigWithChannels();
    KTDiag::EventLogCollector collector;

    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);

    // Should produce .evtx or .txt files for Application and System logs
    // (files land in the eventlogs/ subdirectory, so use recursive iteration)
    bool hasFiles = false;
    for (const auto& entry : fs::recursive_directory_iterator(m_outputDir))
    {
        if (entry.is_regular_file())
            hasFiles = true;
    }
    EXPECT_TRUE(hasFiles);
}
