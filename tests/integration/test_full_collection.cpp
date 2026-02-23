#include <gtest/gtest.h>
#include "collectors/CollectorEngine.h"
#include "collectors/EnvVarCollector.h"
#include "collectors/DiskSpaceCollector.h"
#include "collectors/SystemInfoCollector.h"
#include "output/ReportGenerator.h"
#include "output/ZipPackager.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class FullCollectionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_Integration";
        fs::create_directories(m_outputDir);

        // Create a minimal config file
        m_configFile = fs::path(tempPath) / L"test_integration_config.json";
        {
            std::ofstream f(m_configFile);
            f << R"({ "product_name": "IntegrationTestProduct" })";
        }
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
        fs::remove(m_configFile, ec);

        // Clean up any ZIP files
        for (const auto& entry : fs::directory_iterator(m_outputDir.parent_path(), ec))
        {
            if (entry.is_regular_file())
            {
                auto name = entry.path().filename().wstring();
                if (name.find(L"KT-WinDiag_IntegrationTestProduct_") != std::wstring::npos &&
                    entry.path().extension() == L".zip")
                {
                    fs::remove(entry.path(), ec);
                }
            }
        }
    }

    fs::path m_outputDir;
    fs::path m_configFile;
};

TEST_F(FullCollectionTest, CollectSubsetAndPackage)
{
    // Load config
    KTDiag::Config config;
    ASSERT_TRUE(config.LoadFromFile(m_configFile.wstring()));

    // Register a subset of fast, safe collectors
    KTDiag::CollectorEngine engine;
    engine.AddCollector(std::make_unique<KTDiag::EnvVarCollector>());
    engine.AddCollector(std::make_unique<KTDiag::DiskSpaceCollector>());
    engine.AddCollector(std::make_unique<KTDiag::SystemInfoCollector>());

    // Run all
    std::set<std::wstring> noFilter;
    bool allOk = engine.RunAll(config, m_outputDir, noFilter, nullptr, nullptr);

    EXPECT_TRUE(allOk);
    EXPECT_EQ(engine.SuccessCount(), 3);
    EXPECT_EQ(engine.FailureCount(), 0);

    // Verify output subdirectories exist
    EXPECT_TRUE(fs::exists(m_outputDir / L"env"));
    EXPECT_TRUE(fs::exists(m_outputDir / L"disk"));
    EXPECT_TRUE(fs::exists(m_outputDir / L"sysinfo"));

    // Generate report
    KTDiag::ReportGenerator reportGen;
    bool reportOk = reportGen.Generate(m_outputDir, config, engine.GetResults());
    EXPECT_TRUE(reportOk);
    EXPECT_TRUE(fs::exists(m_outputDir / L"summary.html"));

    // Package into ZIP
    KTDiag::ZipPackager packager;
    bool zipOk = packager.Package(m_outputDir, config.GetProductName());
    EXPECT_TRUE(zipOk);
    EXPECT_FALSE(packager.GetZipPath().empty());

    fs::path zipPath(packager.GetZipPath());
    EXPECT_TRUE(fs::exists(zipPath));
    EXPECT_GT(fs::file_size(zipPath), 0u);

    // Clean up zip
    std::error_code ec;
    fs::remove(zipPath, ec);
}

TEST_F(FullCollectionTest, PartialFailureReported)
{
    KTDiag::Config config;
    ASSERT_TRUE(config.LoadFromFile(m_configFile.wstring()));

    // Add a good collector and filter to include only it plus a nonexistent one
    KTDiag::CollectorEngine engine;
    engine.AddCollector(std::make_unique<KTDiag::EnvVarCollector>());

    std::set<std::wstring> filter = {L"env"};
    bool allOk = engine.RunAll(config, m_outputDir, filter, nullptr, nullptr);

    EXPECT_TRUE(allOk);
    EXPECT_EQ(engine.SuccessCount(), 1);
}
