#include "RegistryCollector.h"
#include "../util/Logger.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

namespace KTDiag
{

bool RegistryCollector::ParseRegistryPath(const std::wstring& fullPath,
                                           HKEY& rootKey, std::wstring& subKeyPath)
{
    // Find first backslash
    size_t pos = fullPath.find(L'\\');
    std::wstring rootStr = (pos != std::wstring::npos) ? fullPath.substr(0, pos) : fullPath;
    subKeyPath = (pos != std::wstring::npos) ? fullPath.substr(pos + 1) : L"";

    if (rootStr == L"HKLM" || rootStr == L"HKEY_LOCAL_MACHINE")
        rootKey = HKEY_LOCAL_MACHINE;
    else if (rootStr == L"HKCU" || rootStr == L"HKEY_CURRENT_USER")
        rootKey = HKEY_CURRENT_USER;
    else if (rootStr == L"HKCR" || rootStr == L"HKEY_CLASSES_ROOT")
        rootKey = HKEY_CLASSES_ROOT;
    else if (rootStr == L"HKU" || rootStr == L"HKEY_USERS")
        rootKey = HKEY_USERS;
    else
        return false;

    return true;
}

std::wstring RegistryCollector::FormatRegValue(DWORD type, const BYTE* data, DWORD dataSize)
{
    switch (type)
    {
    case REG_SZ:
    case REG_EXPAND_SZ:
    {
        if (data && dataSize >= sizeof(wchar_t))
            return std::wstring(reinterpret_cast<const wchar_t*>(data),
                                dataSize / sizeof(wchar_t) - 1);
        return L"";
    }

    case REG_DWORD:
    {
        if (data && dataSize >= sizeof(DWORD))
        {
            DWORD val = *reinterpret_cast<const DWORD*>(data);
            wchar_t buf[32];
            swprintf_s(buf, L"0x%08X (%u)", val, val);
            return buf;
        }
        return L"0";
    }

    case REG_QWORD:
    {
        if (data && dataSize >= sizeof(ULONGLONG))
        {
            ULONGLONG val = *reinterpret_cast<const ULONGLONG*>(data);
            wchar_t buf[64];
            swprintf_s(buf, L"0x%016llX (%llu)", val, val);
            return buf;
        }
        return L"0";
    }

    case REG_MULTI_SZ:
    {
        if (!data || dataSize < sizeof(wchar_t))
            return L"";
        std::wstring result;
        const wchar_t* ptr = reinterpret_cast<const wchar_t*>(data);
        while (*ptr)
        {
            if (!result.empty()) result += L" | ";
            result += ptr;
            ptr += wcslen(ptr) + 1;
        }
        return result;
    }

    case REG_BINARY:
    {
        std::wostringstream oss;
        for (DWORD i = 0; i < dataSize && i < 256; ++i)
        {
            if (i > 0) oss << L' ';
            oss << std::hex << std::setfill(L'0') << std::setw(2) << data[i];
        }
        if (dataSize > 256)
            oss << L" ... (" << dataSize << L" bytes)";
        return oss.str();
    }

    default:
    {
        wchar_t buf[64];
        swprintf_s(buf, L"<type=%u, %u bytes>", type, dataSize);
        return buf;
    }
    }
}

void RegistryCollector::ExportKey(HKEY rootKey, const std::wstring& subKeyPath,
                                   const std::wstring& displayPath,
                                   std::wofstream& file, int depth,
                                   CancelToken cancel)
{
    if (cancel && cancel->load()) return;
    if (depth > 32) return;  // Safety limit

    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(rootKey, subKeyPath.c_str(), 0, KEY_READ, &hKey);
    if (rc != ERROR_SUCCESS)
    {
        file << L"[" << displayPath << L"]  (Access Denied or Not Found, error=" << rc << L")"
             << std::endl << std::endl;
        return;
    }

    file << L"[" << displayPath << L"]" << std::endl;

    // Enumerate values
    DWORD valueCount = 0, maxValueNameLen = 0, maxValueDataLen = 0;
    RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     &valueCount, &maxValueNameLen, &maxValueDataLen, nullptr, nullptr);

    maxValueNameLen += 1;  // null terminator
    std::vector<wchar_t> valueName(maxValueNameLen);
    std::vector<BYTE> valueData(maxValueDataLen);

    for (DWORD i = 0; i < valueCount; ++i)
    {
        if (cancel && cancel->load()) break;

        DWORD nameLen = maxValueNameLen;
        DWORD dataLen = maxValueDataLen;
        DWORD type = 0;

        rc = RegEnumValueW(hKey, i, valueName.data(), &nameLen,
                           nullptr, &type, valueData.data(), &dataLen);
        if (rc == ERROR_SUCCESS)
        {
            std::wstring name = (nameLen > 0) ? valueName.data() : L"(Default)";
            std::wstring value = FormatRegValue(type, valueData.data(), dataLen);

            const wchar_t* typeStr = L"???";
            switch (type)
            {
            case REG_SZ:        typeStr = L"REG_SZ"; break;
            case REG_EXPAND_SZ: typeStr = L"REG_EXPAND_SZ"; break;
            case REG_DWORD:     typeStr = L"REG_DWORD"; break;
            case REG_QWORD:     typeStr = L"REG_QWORD"; break;
            case REG_BINARY:    typeStr = L"REG_BINARY"; break;
            case REG_MULTI_SZ:  typeStr = L"REG_MULTI_SZ"; break;
            case REG_NONE:      typeStr = L"REG_NONE"; break;
            }

            file << L"  " << name << L"  [" << typeStr << L"]  " << value << std::endl;
        }
    }

    file << std::endl;

    // Enumerate subkeys
    DWORD subKeyCount = 0, maxSubKeyLen = 0;
    RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &subKeyCount, &maxSubKeyLen,
                     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    maxSubKeyLen += 1;
    std::vector<wchar_t> subKeyName(maxSubKeyLen);

    for (DWORD i = 0; i < subKeyCount; ++i)
    {
        if (cancel && cancel->load()) break;

        DWORD nameLen = maxSubKeyLen;
        rc = RegEnumKeyExW(hKey, i, subKeyName.data(), &nameLen,
                           nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_SUCCESS)
        {
            std::wstring childSubKey = subKeyPath + L"\\" + subKeyName.data();
            std::wstring childDisplay = displayPath + L"\\" + subKeyName.data();
            ExportKey(rootKey, childSubKey, childDisplay, file, depth + 1, cancel);
        }
    }

    RegCloseKey(hKey);
}

bool RegistryCollector::Collect(const Config& config,
                                 const std::filesystem::path& outputDir,
                                 ProgressCallback cb,
                                 CancelToken cancel)
{
    if (cancel && cancel->load())
    {
        m_error = L"Cancelled";
        return false;
    }

    const auto& regKeys = config.GetRegistryKeys();
    if (regKeys.empty())
    {
        if (cb) cb(Name(), L"No registry keys configured", 100);
        Logger::Instance().Log(LogLevel::Warning, L"[Registry] No registry keys in config");
        return true;
    }

    if (cb) cb(Name(), L"Exporting registry keys...", 0);

    std::filesystem::path outFile = outputDir / L"registry_export.txt";
    std::wofstream file(outFile);
    if (!file.is_open())
    {
        m_error = L"Failed to create output file: " + outFile.wstring();
        return false;
    }

    file << L"=== Registry Export ===" << std::endl;
    file << L"Keys to export: " << regKeys.size() << std::endl;
    file << L"========================================" << std::endl << std::endl;

    int total = static_cast<int>(regKeys.size());
    for (int i = 0; i < total; ++i)
    {
        if (cancel && cancel->load())
        {
            m_error = L"Cancelled";
            return false;
        }

        const auto& keyPath = regKeys[i];
        int pct = (i * 100) / total;
        if (cb) cb(Name(), L"Exporting: " + keyPath, pct);

        HKEY rootKey = nullptr;
        std::wstring subKeyPath;
        if (ParseRegistryPath(keyPath, rootKey, subKeyPath))
        {
            ExportKey(rootKey, subKeyPath, keyPath, file, 0, cancel);
        }
        else
        {
            file << L"[" << keyPath << L"]  (Invalid registry path)" << std::endl << std::endl;
            Logger::Instance().Log(LogLevel::Warning, L"[Registry] Invalid path: %s",
                                   keyPath.c_str());
        }
    }

    file.close();

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[Registry] Exported %d key(s)", total);
    return true;
}

} // namespace KTDiag
