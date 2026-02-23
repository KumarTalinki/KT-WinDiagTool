#include <gtest/gtest.h>
#include "controllers/ReproOrchestrator.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class ReproOrchestratorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_Repro";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(ReproOrchestratorTest, InitialState)
{
    KTDiag::ReproOrchestrator orchestrator;
    EXPECT_FALSE(orchestrator.IsActive());
}

TEST_F(ReproOrchestratorTest, StartStopWithEmptyConfig)
{
    KTDiag::Config config; // No providers configured
    KTDiag::ReproOrchestrator orchestrator;

    auto startResult = orchestrator.StartRepro(config, m_outputDir, 3);
    // With no providers, nothing should actually start — but it shouldn't crash
    auto stopResult = orchestrator.StopRepro();
    EXPECT_FALSE(orchestrator.IsActive());
}

TEST_F(ReproOrchestratorTest, StartRecordsTimestamp)
{
    KTDiag::Config config;
    KTDiag::ReproOrchestrator orchestrator;

    auto before = std::chrono::system_clock::now();
    orchestrator.StartRepro(config, m_outputDir, 3);
    auto startTime = orchestrator.GetStartTime();
    orchestrator.StopRepro();
    auto stopTime = orchestrator.GetStopTime();

    EXPECT_GE(startTime, before);
    EXPECT_GE(stopTime, startTime);
}
