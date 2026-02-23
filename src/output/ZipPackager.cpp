#include "ZipPackager.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <zip.h>
#include <iowin32.h>

#include <fstream>
#include <vector>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

namespace
{

constexpr size_t READ_BUFFER_SIZE = 65536; // 64 KB

std::wstring GetTimestampString()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wostringstream oss;
    oss << st.wYear
        << std::setfill(L'0') << std::setw(2) << st.wMonth
        << std::setw(2) << st.wDay << L"_"
        << std::setw(2) << st.wHour
        << std::setw(2) << st.wMinute
        << std::setw(2) << st.wSecond;
    return oss.str();
}

std::wstring SanitizeProductName(const std::wstring& name)
{
    std::wstring result = name;
    const std::wstring invalidChars = L"/\\:*?\"<>| ";
    for (auto& ch : result)
    {
        if (invalidChars.find(ch) != std::wstring::npos)
            ch = L'_';
    }
    if (result.size() > 50)
        result.resize(50);
    return result;
}

// Convert wide string to UTF-8 for minizip entry names
std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return std::string();

    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                   static_cast<int>(wide.size()), nullptr, 0,
                                   nullptr, nullptr);
    if (len <= 0)
        return std::string();

    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()), &utf8[0], len,
                        nullptr, nullptr);
    return utf8;
}

// Normalize path separators to forward slash for ZIP entries
std::wstring NormalizeZipPath(const std::wstring& path)
{
    std::wstring result = path;
    for (auto& ch : result)
    {
        if (ch == L'\\')
            ch = L'/';
    }
    return result;
}

} // anonymous namespace

namespace KTDiag
{

bool ZipPackager::AddFileToZip(zipFile zf,
                                const fs::path& filePath,
                                const std::wstring& entryName)
{
    // Convert entry name to UTF-8 with forward slashes
    std::string utf8Name = WideToUtf8(NormalizeZipPath(entryName));
    if (utf8Name.empty())
        return false;

    // Get file timestamp
    zip_fileinfo zi = {};
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(filePath.wstring().c_str(), GetFileExInfoStandard, &fad))
    {
        FILETIME localFt;
        FileTimeToLocalFileTime(&fad.ftLastWriteTime, &localFt);
        SYSTEMTIME st;
        FileTimeToSystemTime(&localFt, &st);
        zi.tmz_date.tm_year = st.wYear;
        zi.tmz_date.tm_mon = st.wMonth - 1; // 0-based
        zi.tmz_date.tm_mday = st.wDay;
        zi.tmz_date.tm_hour = st.wHour;
        zi.tmz_date.tm_min = st.wMinute;
        zi.tmz_date.tm_sec = st.wSecond;
    }

    int err = zipOpenNewFileInZip(zf, utf8Name.c_str(), &zi,
                                   nullptr, 0, nullptr, 0, nullptr,
                                   Z_DEFLATED, Z_DEFAULT_COMPRESSION);
    if (err != ZIP_OK)
    {
        Logger::Instance().Log(LogLevel::Warning,
            L"[ZIP] Failed to add entry: %s", entryName.c_str());
        return false;
    }

    // Read file and write to ZIP
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs)
    {
        zipCloseFileInZip(zf);
        return false;
    }

    std::vector<char> buffer(READ_BUFFER_SIZE);
    while (ifs)
    {
        ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto bytesRead = ifs.gcount();
        if (bytesRead > 0)
        {
            err = zipWriteInFileInZip(zf, buffer.data(), static_cast<unsigned int>(bytesRead));
            if (err != ZIP_OK)
            {
                Logger::Instance().Log(LogLevel::Warning,
                    L"[ZIP] Write error for: %s", entryName.c_str());
                zipCloseFileInZip(zf);
                return false;
            }
        }
    }

    zipCloseFileInZip(zf);
    return true;
}

bool ZipPackager::Package(const fs::path& outputDir,
                           const std::wstring& productName)
{
    m_error.clear();

    if (!fs::exists(outputDir) || !fs::is_directory(outputDir))
    {
        m_error = L"Output directory does not exist";
        return false;
    }

    // Build ZIP filename
    std::wstring safeName = SanitizeProductName(productName);
    std::wstring zipFilename = L"KT-WinDiag_" + safeName + L"_" + GetTimestampString() + L".zip";

    // Place ZIP in parent directory of outputDir
    m_zipPath = outputDir.parent_path() / zipFilename;

    Logger::Instance().Log(LogLevel::Info,
        L"[ZIP] Creating package: %s", m_zipPath.wstring().c_str());

    // Open ZIP with wide-string path support
    zlib_filefunc64_def ffunc;
    fill_win32_filefunc64W(&ffunc);

    zipFile zf = zipOpen2_64(m_zipPath.wstring().c_str(),
                              APPEND_STATUS_CREATE, nullptr, &ffunc);
    if (!zf)
    {
        m_error = L"Failed to create ZIP file: " + m_zipPath.wstring();
        return false;
    }

    // Get the directory name for ZIP entry prefix
    std::wstring dirName = outputDir.filename().wstring();

    int fileCount = 0;
    int errorCount = 0;
    std::error_code ec;

    for (const auto& entry : fs::recursive_directory_iterator(outputDir,
             fs::directory_options::skip_permission_denied, ec))
    {
        if (!entry.is_regular_file())
            continue;

        // Compute relative path within outputDir
        fs::path relativePath = fs::relative(entry.path(), outputDir, ec);
        if (ec)
            continue;

        // Entry name includes the top-level directory name
        std::wstring entryName = dirName + L"/" + relativePath.wstring();

        if (AddFileToZip(zf, entry.path(), entryName))
        {
            ++fileCount;
        }
        else
        {
            ++errorCount;
        }
    }

    zipClose(zf, nullptr);

    Logger::Instance().Log(LogLevel::Info,
        L"[ZIP] Package created: %s (%d files, %d errors)",
        m_zipPath.wstring().c_str(), fileCount, errorCount);

    if (fileCount == 0)
    {
        m_error = L"No files were added to the ZIP";
        return false;
    }

    // Log file size
    auto zipSize = fs::file_size(m_zipPath, ec);
    if (!ec)
    {
        if (zipSize >= 1024 * 1024)
        {
            Logger::Instance().Log(LogLevel::Info,
                L"[ZIP] Package size: %llu MB", zipSize / (1024 * 1024));
        }
        else
        {
            Logger::Instance().Log(LogLevel::Info,
                L"[ZIP] Package size: %llu KB", zipSize / 1024);
        }
    }

    return true;
}

} // namespace KTDiag
