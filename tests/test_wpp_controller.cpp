#include <gtest/gtest.h>
#include "controllers/WppController.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class WppControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_WppCtrl";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(WppControllerTest, InitialState)
{
    KTDiag::WppController controller;
    EXPECT_FALSE(controller.IsActive());
    EXPECT_TRUE(controller.GetError().empty());
    EXPECT_TRUE(controller.GetEtlPaths().empty());
}

TEST_F(WppControllerTest, StartWithEmptyConfigDoesNotCrash)
{
    KTDiag::WppController controller;
    KTDiag::Config config; // No WPP providers

    // Start should return success (nothing to start) or graceful failure
    bool result = controller.Start(config, 3, m_outputDir);
    // Either way, cleanup should work
    if (controller.IsActive())
        controller.Stop();
}

TEST_F(WppControllerTest, StopWhenNotActive)
{
    KTDiag::WppController controller;
    // Stopping when not active should not crash
    bool result = controller.Stop();
    // No assertion on result — just verify no crash
    EXPECT_FALSE(controller.IsActive());
}

TEST_F(WppControllerTest, DefaultLevel)
{
    KTDiag::WppController controller;
    EXPECT_EQ(controller.CurrentLevel(), 3); // Default is Warning
}
