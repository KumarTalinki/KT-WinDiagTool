#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace KTDiag
{

class StringUtils
{
public:
    // Convert UTF-8 string to wide string (UTF-16)
    static std::wstring Utf8ToWide(const std::string& utf8);

    // Convert wide string (UTF-16) to UTF-8
    static std::string WideToUtf8(const std::wstring& wide);

    // Convert a GUID string like "{XXXXXXXX-XXXX-...}" to a GUID struct
    static bool StringToGuid(const std::wstring& str, GUID& guid);

    // Convert a GUID struct to string representation
    static std::wstring GuidToString(const GUID& guid);

    // Format a byte count as human-readable (e.g., "1.5 GB")
    static std::wstring FormatBytes(uint64_t bytes);

    // Format a FILETIME as readable timestamp
    static std::wstring FormatFileTime(const FILETIME& ft);

    // Split a string by delimiter
    static std::vector<std::wstring> Split(const std::wstring& str, wchar_t delimiter);

    // Trim whitespace from both ends
    static std::wstring Trim(const std::wstring& str);

    // Case-insensitive comparison
    static bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b);
};

} // namespace KTDiag
