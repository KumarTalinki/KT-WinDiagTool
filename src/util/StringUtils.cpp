#include "StringUtils.h"

#include <objbase.h>
#include <cstdio>
#include <algorithm>
#include <cwctype>

namespace KTDiag
{

std::wstring StringUtils::Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
        return {};

    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0)
        return {};

    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &result[0], len);
    return result;
}

std::string StringUtils::WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};

    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};

    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                        &result[0], len, nullptr, nullptr);
    return result;
}

bool StringUtils::StringToGuid(const std::wstring& str, GUID& guid)
{
    return SUCCEEDED(CLSIDFromString(str.c_str(), &guid));
}

std::wstring StringUtils::GuidToString(const GUID& guid)
{
    wchar_t buf[64] = {};
    swprintf_s(buf, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
               guid.Data1, guid.Data2, guid.Data3,
               guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
               guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buf;
}

std::wstring StringUtils::FormatBytes(uint64_t bytes)
{
    wchar_t buf[64] = {};
    if (bytes >= 1099511627776ULL)
        swprintf_s(buf, L"%.2f TB", static_cast<double>(bytes) / 1099511627776.0);
    else if (bytes >= 1073741824ULL)
        swprintf_s(buf, L"%.2f GB", static_cast<double>(bytes) / 1073741824.0);
    else if (bytes >= 1048576ULL)
        swprintf_s(buf, L"%.2f MB", static_cast<double>(bytes) / 1048576.0);
    else if (bytes >= 1024ULL)
        swprintf_s(buf, L"%.2f KB", static_cast<double>(bytes) / 1024.0);
    else
        swprintf_s(buf, L"%llu B", bytes);
    return buf;
}

std::wstring StringUtils::FormatFileTime(const FILETIME& ft)
{
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);

    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::vector<std::wstring> StringUtils::Split(const std::wstring& str, wchar_t delimiter)
{
    std::vector<std::wstring> result;
    size_t start = 0;
    size_t pos = 0;

    while ((pos = str.find(delimiter, start)) != std::wstring::npos)
    {
        result.push_back(str.substr(start, pos - start));
        start = pos + 1;
    }

    if (start <= str.size())
        result.push_back(str.substr(start));

    return result;
}

std::wstring StringUtils::Trim(const std::wstring& str)
{
    auto start = std::find_if_not(str.begin(), str.end(), std::iswspace);
    auto end = std::find_if_not(str.rbegin(), str.rend(), std::iswspace).base();

    if (start >= end)
        return {};

    return std::wstring(start, end);
}

bool StringUtils::EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
{
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

} // namespace KTDiag
