#include <gtest/gtest.h>
#include "app/Config.h"
#include "util/Logger.h"

#include <Windows.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

class ConfigTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");

        // Find the config directory relative to the test executable
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        m_exeDir = fs::path(exePath).parent_path();
        m_configDir = m_exeDir / "config";
    }

    fs::path m_exeDir;
    fs::path m_configDir;
};

TEST_F(ConfigTest, LoadSampleProduct)
{
    KTDiag::Config config;
    fs::path configPath = m_configDir / "sample_product.json";

    // Skip if config not copied yet
    if (!fs::exists(configPath))
        GTEST_SKIP() << "Config file not found at: " << configPath;

    ASSERT_TRUE(config.LoadFromFile(configPath.wstring()));
    EXPECT_EQ(config.GetProductName(), L"ExampleAV");
    EXPECT_FALSE(config.GetServiceNames().empty());
    EXPECT_FALSE(config.GetRegistryKeys().empty());
    EXPECT_FALSE(config.GetEventLogChannels().empty());
    EXPECT_FALSE(config.GetWppProviders().empty());
    EXPECT_FALSE(config.GetWppTraceLevels().available.empty());
}

TEST_F(ConfigTest, LoadInvalidPath)
{
    KTDiag::Config config;
    EXPECT_FALSE(config.LoadFromFile(L"C:\\nonexistent\\path\\config.json"));
}

TEST_F(ConfigTest, LoadInvalidJson)
{
    // Create a temp file with invalid JSON
    fs::path tempFile = m_exeDir / "test_invalid.json";
    {
        std::ofstream f(tempFile);
        f << "{ this is not valid json }}}";
    }

    KTDiag::Config config;
    EXPECT_FALSE(config.LoadFromFile(tempFile.wstring()));

    fs::remove(tempFile);
}

TEST_F(ConfigTest, LoadMissingProductName)
{
    fs::path tempFile = m_exeDir / "test_no_name.json";
    {
        std::ofstream f(tempFile);
        f << R"({ "service_names": ["svc1"] })";
    }

    KTDiag::Config config;
    EXPECT_FALSE(config.LoadFromFile(tempFile.wstring()));

    fs::remove(tempFile);
}

TEST_F(ConfigTest, ExpandEnvironmentPath)
{
    std::wstring expanded = KTDiag::Config::ExpandEnvironmentPath(L"%SystemRoot%\\System32");
    EXPECT_NE(expanded.find(L"System32"), std::wstring::npos);
    EXPECT_EQ(expanded.find(L"%"), std::wstring::npos);  // No unexpanded vars
}

TEST_F(ConfigTest, DefaultEventLogChannels)
{
    fs::path tempFile = m_exeDir / "test_minimal.json";
    {
        std::ofstream f(tempFile);
        f << R"({ "product_name": "MinimalProduct" })";
    }

    KTDiag::Config config;
    ASSERT_TRUE(config.LoadFromFile(tempFile.wstring()));

    // Should have default Application + System channels
    const auto& channels = config.GetEventLogChannels();
    EXPECT_GE(channels.size(), 2u);

    fs::remove(tempFile);
}

TEST_F(ConfigTest, DefaultWppTraceLevels)
{
    fs::path tempFile = m_exeDir / "test_no_levels.json";
    {
        std::ofstream f(tempFile);
        f << R"({ "product_name": "NoLevelsProduct" })";
    }

    KTDiag::Config config;
    ASSERT_TRUE(config.LoadFromFile(tempFile.wstring()));

    // Should have default trace levels
    const auto& levels = config.GetWppTraceLevels();
    EXPECT_EQ(levels.available.size(), 5u);
    EXPECT_EQ(levels.defaultLevel, L"Warning");

    fs::remove(tempFile);
}

TEST_F(ConfigTest, IsValidAfterLoad)
{
    fs::path tempFile = m_exeDir / "test_valid.json";
    {
        std::ofstream f(tempFile);
        f << R"({ "product_name": "TestProduct" })";
    }

    KTDiag::Config config;
    EXPECT_FALSE(config.IsValid());  // Before load

    ASSERT_TRUE(config.LoadFromFile(tempFile.wstring()));
    EXPECT_TRUE(config.IsValid());   // After load

    fs::remove(tempFile);
}
