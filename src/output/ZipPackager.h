#pragma once

#include <string>
#include <filesystem>

// Forward declare minizip type to avoid including zip.h in header
typedef void* voidp;
typedef voidp zipFile;

namespace KTDiag
{

class ZipPackager
{
public:
    bool Package(const std::filesystem::path& outputDir,
                 const std::wstring& productName);

    std::wstring GetZipPath() const { return m_zipPath.wstring(); }
    std::wstring GetError() const { return m_error; }

private:
    bool AddFileToZip(zipFile zf,
                      const std::filesystem::path& filePath,
                      const std::wstring& entryName);

    std::filesystem::path m_zipPath;
    std::wstring m_error;
};

} // namespace KTDiag
