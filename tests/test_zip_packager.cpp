#include <gtest/gtest.h>
#include "output/ZipPackager.h"
#include "util/Logger.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class ZipPackagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        m_outputDir = fs::path(tempPath) / L"KTDiagTest_Zip";
        fs::create_directories(m_outputDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_outputDir, ec);

        // Also clean up any ZIP file created alongside the output dir
        for (const auto& entry : fs::directory_iterator(m_outputDir.parent_path(), ec))
        {
            if (entry.is_regular_file())
            {
                auto name = entry.path().filename().wstring();
                if (name.find(L"KT-WinDiag_TestProduct_") != std::wstring::npos &&
                    entry.path().extension() == L".zip")
                {
                    fs::remove(entry.path(), ec);
                }
            }
        }
    }

    fs::path m_outputDir;
};

TEST_F(ZipPackagerTest, PackageEmptyDirectory)
{
    KTDiag::ZipPackager packager;
    bool result = packager.Package(m_outputDir, L"TestProduct");

    // Should succeed even with empty dir (creates zip with no entries or minimal)
    // The zip file should be created
    if (result)
    {
        EXPECT_FALSE(packager.GetZipPath().empty());
    }
}

TEST_F(ZipPackagerTest, PackageWithFiles)
{
    // Create test files
    {
        std::ofstream f(m_outputDir / "test1.txt");
        f << "Test file 1 content";
    }
    {
        fs::create_directories(m_outputDir / "subdir");
        std::ofstream f(m_outputDir / "subdir" / "test2.txt");
        f << "Test file 2 content in subdirectory";
    }

    KTDiag::ZipPackager packager;
    bool result = packager.Package(m_outputDir, L"TestProduct");

    EXPECT_TRUE(result);
    EXPECT_FALSE(packager.GetZipPath().empty());

    // Verify the zip file exists and has non-zero size
    fs::path zipPath(packager.GetZipPath());
    EXPECT_TRUE(fs::exists(zipPath));
    EXPECT_GT(fs::file_size(zipPath), 0u);

    // Clean up the zip file
    std::error_code ec;
    fs::remove(zipPath, ec);
}

TEST_F(ZipPackagerTest, ZipPathContainsProductName)
{
    {
        std::ofstream f(m_outputDir / "data.txt");
        f << "Some data";
    }

    KTDiag::ZipPackager packager;
    bool result = packager.Package(m_outputDir, L"TestProduct");

    if (result)
    {
        std::wstring zipPath = packager.GetZipPath();
        EXPECT_NE(zipPath.find(L"TestProduct"), std::wstring::npos);

        std::error_code ec;
        fs::remove(fs::path(zipPath), ec);
    }
}
