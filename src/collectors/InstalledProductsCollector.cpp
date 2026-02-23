#include "InstalledProductsCollector.h"
#include "../util/Logger.h"

#include <Windows.h>
#include <fstream>

namespace KTDiag
{

static std::wstring ReadRegString(HKEY hKey, const wchar_t* valueName)
{
    DWORD type = 0, dataSize = 0;
    if (RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &dataSize) != ERROR_SUCCESS)
        return L"";
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return L"";

    std::wstring result(dataSize / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(hKey, valueName, nullptr, nullptr,
                         reinterpret_cast<BYTE*>(&result[0]), &dataSize) != ERROR_SUCCESS)
        return L"";

    // Remove trailing null
    while (!result.empty() && result.back() == L'\0')
        result.pop_back();

    return result;
}

void InstalledProductsCollector::EnumerateFromRegistry(HKEY rootKey,
                                                        const std::wstring& subKey,
                                                        std::wofstream& file,
                                                        int& count,
                                                        CancelToken cancel)
{
    HKEY hUninstall = nullptr;
    if (RegOpenKeyExW(rootKey, subKey.c_str(), 0, KEY_READ, &hUninstall) != ERROR_SUCCESS)
        return;

    DWORD subKeyCount = 0;
    RegQueryInfoKeyW(hUninstall, nullptr, nullptr, nullptr, &subKeyCount,
                     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    for (DWORD i = 0; i < subKeyCount; ++i)
    {
        if (cancel && cancel->load()) break;

        wchar_t keyName[256] = {};
        DWORD keyNameLen = _countof(keyName);
        if (RegEnumKeyExW(hUninstall, i, keyName, &keyNameLen,
                          nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            continue;

        HKEY hProduct = nullptr;
        if (RegOpenKeyExW(hUninstall, keyName, 0, KEY_READ, &hProduct) != ERROR_SUCCESS)
            continue;

        std::wstring displayName = ReadRegString(hProduct, L"DisplayName");
        if (!displayName.empty())  // Skip entries without a display name
        {
            std::wstring version = ReadRegString(hProduct, L"DisplayVersion");
            std::wstring publisher = ReadRegString(hProduct, L"Publisher");
            std::wstring installDate = ReadRegString(hProduct, L"InstallDate");
            std::wstring installLocation = ReadRegString(hProduct, L"InstallLocation");

            file << displayName;
            if (!version.empty()) file << L"  |  Version: " << version;
            if (!publisher.empty()) file << L"  |  Publisher: " << publisher;
            if (!installDate.empty()) file << L"  |  Installed: " << installDate;
            if (!installLocation.empty()) file << L"  |  Location: " << installLocation;
            file << std::endl;

            ++count;
        }

        RegCloseKey(hProduct);
    }

    RegCloseKey(hUninstall);
}

bool InstalledProductsCollector::Collect(const Config& config,
                                          const std::filesystem::path& outputDir,
                                          ProgressCallback cb,
                                          CancelToken cancel)
{
    UNREFERENCED_PARAMETER(config);
    if (cb) cb(Name(), L"Enumerating installed products...", 0);

    std::filesystem::path outFile = outputDir / L"installed_products.txt";
    std::wofstream file(outFile);
    if (!file.is_open())
    {
        m_error = L"Failed to create output file: " + outFile.wstring();
        return false;
    }

    file << L"=== Installed Products ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    int count = 0;

    // 64-bit programs
    if (cb) cb(Name(), L"Scanning 64-bit programs...", 20);
    file << L"--- 64-bit Programs ---" << std::endl;
    EnumerateFromRegistry(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        file, count, cancel);
    file << std::endl;

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }

    // 32-bit programs (WoW64)
    if (cb) cb(Name(), L"Scanning 32-bit programs...", 50);
    file << L"--- 32-bit Programs (WoW64) ---" << std::endl;
    EnumerateFromRegistry(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        file, count, cancel);
    file << std::endl;

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }

    // Current user programs
    if (cb) cb(Name(), L"Scanning user-installed programs...", 75);
    file << L"--- Current User Programs ---" << std::endl;
    EnumerateFromRegistry(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        file, count, cancel);
    file << std::endl;

    // Write total at top — rewind not practical with wofstream, so append summary
    file << L"========================================" << std::endl;
    file << L"Total products found: " << count << std::endl;
    file.close();

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[InstalledProducts] Found %d products", count);
    return true;
}

} // namespace KTDiag
