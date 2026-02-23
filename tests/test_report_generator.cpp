#include <gtest/gtest.h>
#include "output/ReportGenerator.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

class ReportGeneratorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_Report";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);
    }

    fs::path m_outputDir;
};

TEST_F(ReportGeneratorTest, GenerateWithEmptyResults)
{
    KTDiag::Config config;
    std::vector<KTDiag::CollectorResult> results;

    KTDiag::ReportGenerator generator;
    bool result = generator.Generate(m_outputDir, config, results);

    EXPECT_TRUE(result);

    fs::path summaryFile = m_outputDir / L"summary.html";
    EXPECT_TRUE(fs::exists(summaryFile));
    EXPECT_GT(fs::file_size(summaryFile), 0u);
}

TEST_F(ReportGeneratorTest, GenerateWithMixedResults)
{
    // Create a minimal config
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    fs::path configFile = fs::path(tempPath) / L"test_report_config.json";
    {
        std::ofstream f(configFile);
        f << R"({ "product_name": "ReportTestProduct" })";
    }

    KTDiag::Config config;
    config.LoadFromFile(configFile.wstring());
    fs::remove(configFile);

    // Create mixed results
    std::vector<KTDiag::CollectorResult> results;

    KTDiag::CollectorResult ok;
    ok.collectorName = L"EnvVars";
    ok.collectorId = L"env";
    ok.success = true;
    ok.elapsedSeconds = 0.5;
    results.push_back(ok);

    KTDiag::CollectorResult fail;
    fail.collectorName = L"Registry";
    fail.collectorId = L"registry";
    fail.success = false;
    fail.errorMessage = L"Access denied";
    fail.elapsedSeconds = 1.2;
    results.push_back(fail);

    KTDiag::ReportGenerator generator;
    bool result = generator.Generate(m_outputDir, config, results);

    EXPECT_TRUE(result);

    fs::path summaryFile = m_outputDir / L"summary.html";
    EXPECT_TRUE(fs::exists(summaryFile));

    // Verify the HTML contains expected content
    std::wifstream ifs(summaryFile);
    std::wstring content((std::istreambuf_iterator<wchar_t>(ifs)),
                          std::istreambuf_iterator<wchar_t>());

    EXPECT_NE(content.find(L"html"), std::wstring::npos);
    EXPECT_NE(content.find(L"EnvVars"), std::wstring::npos);
    EXPECT_NE(content.find(L"Registry"), std::wstring::npos);
}

TEST_F(ReportGeneratorTest, GenerateIncludesArtifactsList)
{
    // Create some artifacts in the output dir
    fs::create_directories(m_outputDir / L"env");
    {
        std::ofstream f(m_outputDir / L"env" / L"env_vars.txt");
        f << "PATH=C:\\Windows";
    }

    KTDiag::Config config;
    std::vector<KTDiag::CollectorResult> results;

    KTDiag::ReportGenerator generator;
    bool result = generator.Generate(m_outputDir, config, results);

    EXPECT_TRUE(result);
    EXPECT_TRUE(fs::exists(m_outputDir / L"summary.html"));
}
