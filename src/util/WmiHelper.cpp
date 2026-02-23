#include "WmiHelper.h"
#include "Logger.h"

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace KTDiag
{

WmiHelper::WmiHelper() = default;

WmiHelper::~WmiHelper()
{
    if (m_services)
    {
        m_services->Release();
        m_services = nullptr;
    }
    if (m_locator)
    {
        m_locator->Release();
        m_locator = nullptr;
    }
    if (m_comInitialized)
    {
        CoUninitialize();
        m_comInitialized = false;
    }
}

bool WmiHelper::Connect(const std::wstring& wmiNamespace)
{
    // Initialize COM on this thread if not already done
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hrInit))
        m_comInitialized = true;
    // S_FALSE means COM was already initialized — that's fine, but don't uninit later
    // RPC_E_CHANGED_MODE means COM was init'd with different mode — still usable
    if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE)
    {
        m_lastError = L"COM initialization failed";
        Logger::Instance().Log(LogLevel::Error, L"WMI: %s (hr=0x%08X)", m_lastError.c_str(), hrInit);
        return false;
    }

    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void**>(&m_locator));
    if (FAILED(hr))
    {
        m_lastError = L"Failed to create WbemLocator";
        Logger::Instance().Log(LogLevel::Error, L"WMI: %s (hr=0x%08X)", m_lastError.c_str(), hr);
        return false;
    }

    hr = m_locator->ConnectServer(
        _bstr_t(wmiNamespace.c_str()),
        nullptr, nullptr, nullptr, 0, nullptr, nullptr,
        &m_services);
    if (FAILED(hr))
    {
        m_lastError = L"Failed to connect to WMI namespace: " + wmiNamespace;
        Logger::Instance().Log(LogLevel::Error, L"WMI: %s (hr=0x%08X)", m_lastError.c_str(), hr);
        return false;
    }

    // Set security on the proxy
    hr = CoSetProxyBlanket(m_services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                           nullptr, EOAC_NONE);
    if (FAILED(hr))
    {
        m_lastError = L"Failed to set WMI proxy security";
        Logger::Instance().Log(LogLevel::Warning, L"WMI: %s (hr=0x%08X)", m_lastError.c_str(), hr);
        // Continue anyway - may still work
    }

    m_connected = true;
    return true;
}

static std::wstring VariantToString(VARIANT& var)
{
    if (var.vt == VT_NULL || var.vt == VT_EMPTY)
        return L"";

    if (var.vt == VT_BSTR)
        return var.bstrVal ? var.bstrVal : L"";

    if (var.vt == VT_I4)
        return std::to_wstring(var.lVal);

    if (var.vt == VT_UI4)
        return std::to_wstring(var.ulVal);

    if (var.vt == VT_I2)
        return std::to_wstring(var.iVal);

    if (var.vt == VT_UI2)
        return std::to_wstring(var.uiVal);

    if (var.vt == VT_BOOL)
        return var.boolVal ? L"True" : L"False";

    if (var.vt == VT_UI1)
        return std::to_wstring(var.bVal);

    if (var.vt == VT_I8)
        return std::to_wstring(var.llVal);

    if (var.vt == VT_UI8)
        return std::to_wstring(var.ullVal);

    // For arrays and other types, try VariantChangeType to string
    VARIANT strVar;
    VariantInit(&strVar);
    if (SUCCEEDED(VariantChangeType(&strVar, &var, 0, VT_BSTR)))
    {
        std::wstring result = strVar.bstrVal ? strVar.bstrVal : L"";
        VariantClear(&strVar);
        return result;
    }

    return L"<unsupported type>";
}

bool WmiHelper::Query(const std::wstring& wql,
                      const std::vector<std::wstring>& fields,
                      std::vector<WmiRow>& results)
{
    if (!m_connected)
    {
        m_lastError = L"Not connected to WMI";
        return false;
    }

    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr = m_services->ExecQuery(
        _bstr_t(L"WQL"),
        _bstr_t(wql.c_str()),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        &enumerator);

    if (FAILED(hr))
    {
        m_lastError = L"WMI query failed: " + wql;
        Logger::Instance().Log(LogLevel::Error, L"WMI: %s (hr=0x%08X)", m_lastError.c_str(), hr);
        return false;
    }

    IWbemClassObject* obj = nullptr;
    ULONG returned = 0;

    while (enumerator->Next(WBEM_INFINITE, 1, &obj, &returned) == S_OK && returned > 0)
    {
        WmiRow row;
        for (const auto& field : fields)
        {
            VARIANT val;
            VariantInit(&val);
            hr = obj->Get(field.c_str(), 0, &val, nullptr, nullptr);
            if (SUCCEEDED(hr))
            {
                row[field] = VariantToString(val);
                VariantClear(&val);
            }
        }
        results.push_back(std::move(row));
        obj->Release();
    }

    enumerator->Release();
    return true;
}

bool WmiHelper::QueryRaw(const std::wstring& wql,
                         std::function<void(IWbemClassObject*)> callback)
{
    if (!m_connected)
    {
        m_lastError = L"Not connected to WMI";
        return false;
    }

    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr = m_services->ExecQuery(
        _bstr_t(L"WQL"),
        _bstr_t(wql.c_str()),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        &enumerator);

    if (FAILED(hr))
    {
        m_lastError = L"WMI query failed: " + wql;
        return false;
    }

    IWbemClassObject* obj = nullptr;
    ULONG returned = 0;

    while (enumerator->Next(WBEM_INFINITE, 1, &obj, &returned) == S_OK && returned > 0)
    {
        callback(obj);
        obj->Release();
    }

    enumerator->Release();
    return true;
}

} // namespace KTDiag
