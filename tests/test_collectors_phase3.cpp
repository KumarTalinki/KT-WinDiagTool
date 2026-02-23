#include <gtest/gtest.h>
#include "collectors/EventLogCollector.h"
#include "collectors/EtwLogCollector.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// EventLogCollector Tests
// ============================================================================

class EventLogCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");

        // Create a temp output directory
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_EventLog";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(EventLogCollectorTest, NameAndId)
{
    KTDiag::EventLogCollector collector;
    EXPECT_EQ(collector.Name(), L"Event Logs");
    EXPECT_EQ(collector.Id(), L"eventlog");
}

TEST_F(EventLogCollectorTest, EmptyChannelsSucceeds)
{
    KTDiag::Config config; // Unloaded config — empty channels
    KTDiag::EventLogCollector collector;

    bool result = collector.Collect(config, m_outputDir, nullptr, nullptr);
    EXPECT_TRUE(result);
    EXPECT_TRUE(collector.GetError().empty());
}

TEST_F(EventLogCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::EventLogCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true); // Pre-cancelled

    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}

// ============================================================================
// EtwLogCollector Tests
// ============================================================================

class EtwLogCollectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");

        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_ETW";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(EtwLogCollectorTest, NameAndId)
{
    KTDiag::EtwLogCollector collector;
    EXPECT_EQ(collector.Name(), L"ETW Traces");
    EXPECT_EQ(collector.Id(), L"etw");
}

TEST_F(EtwLogCollectorTest, CancellationStopsCollection)
{
    KTDiag::Config config;
    KTDiag::EtwLogCollector collector;

    auto cancelToken = std::make_shared<std::atomic<bool>>(true); // Pre-cancelled

    bool result = collector.Collect(config, m_outputDir, nullptr, cancelToken);
    EXPECT_FALSE(result);
    EXPECT_EQ(collector.GetError(), L"Cancelled");
}
