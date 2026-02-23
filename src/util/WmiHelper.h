#pragma once

#include <Windows.h>
#include <Wbemidl.h>
#include <comdef.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace KTDiag
{

// Represents a single row from a WMI query result
using WmiRow = std::map<std::wstring, std::wstring>;

class WmiHelper
{
public:
    WmiHelper();
    ~WmiHelper();

    // Connect to WMI on the local machine
    bool Connect(const std::wstring& wmiNamespace = L"ROOT\\CIMV2");

    // Execute a WQL query and return all rows
    // e.g., Query(L"SELECT * FROM Win32_OperatingSystem", {L"Caption", L"Version"})
    bool Query(const std::wstring& wql,
               const std::vector<std::wstring>& fields,
               std::vector<WmiRow>& results);

    // Execute a query and call a callback for each result object
    bool QueryRaw(const std::wstring& wql,
                  std::function<void(IWbemClassObject*)> callback);

    // Get last error message
    const std::wstring& GetLastError() const { return m_lastError; }

private:
    IWbemLocator* m_locator = nullptr;
    IWbemServices* m_services = nullptr;
    std::wstring m_lastError;
    bool m_connected = false;
    bool m_comInitialized = false;
};

} // namespace KTDiag
