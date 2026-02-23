#include <gtest/gtest.h>
#include "controllers/PerfController.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

class PerfControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_PerfCtrl";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(PerfControllerTest, InitialState)
{
    KTDiag::PerfController controller;
    EXPECT_FALSE(controller.IsActive());
    EXPECT_TRUE(controller.GetError().empty());
}

TEST_F(PerfControllerTest, StartWithEmptyConfigDoesNotCrash)
{
    KTDiag::PerfController controller;
    KTDiag::Config config; // No perf counters

    bool result = controller.Start(config, 1000, m_outputDir);
    if (controller.IsActive())
        controller.Stop();
}

TEST_F(PerfControllerTest, StopWhenNotActive)
{
    KTDiag::PerfController controller;
    bool result = controller.Stop();
    UNREFERENCED_PARAMETER(result);
    EXPECT_FALSE(controller.IsActive());
}
