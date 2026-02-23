#include "ServicesCollector.h"
#include "../util/Logger.h"
#include "../util/StringUtils.h"

#include <vector>
#include <algorithm>

namespace KTDiag
{

const wchar_t* ServicesCollector::StateToString(DWORD state)
{
    switch (state)
    {
    case SERVICE_STOPPED:          return L"Stopped";
    case SERVICE_START_PENDING:    return L"Start Pending";
    case SERVICE_STOP_PENDING:     return L"Stop Pending";
    case SERVICE_RUNNING:          return L"Running";
    case SERVICE_CONTINUE_PENDING: return L"Continue Pending";
    case SERVICE_PAUSE_PENDING:    return L"Pause Pending";
    case SERVICE_PAUSED:           return L"Paused";
    default:                       return L"Unknown";
    }
}

const wchar_t* ServicesCollector::StartTypeToString(DWORD startType)
{
    switch (startType)
    {
    case SERVICE_BOOT_START:   return L"Boot";
    case SERVICE_SYSTEM_START: return L"System";
    case SERVICE_AUTO_START:   return L"Automatic";
    case SERVICE_DEMAND_START: return L"Manual";
    case SERVICE_DISABLED:     return L"Disabled";
    default:                   return L"Unknown";
    }
}

bool ServicesCollector::QueryServiceDetails(SC_HANDLE hSCM,
                                             const std::wstring& serviceName,
                                             ServiceInfo& info)
{
    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName.c_str(),
                                   SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
    if (!hSvc) return false;

    // Query config (binary path, start type, account)
    DWORD bytesNeeded = 0;
    QueryServiceConfigW(hSvc, nullptr, 0, &bytesNeeded);
    if (bytesNeeded > 0)
    {
        std::vector<BYTE> buf(bytesNeeded);
        auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
        if (QueryServiceConfigW(hSvc, cfg, bytesNeeded, &bytesNeeded))
        {
            info.startType = cfg->dwStartType;
            if (cfg->lpBinaryPathName) info.binaryPath = cfg->lpBinaryPathName;
            if (cfg->lpServiceStartName) info.serviceAccount = cfg->lpServiceStartName;
        }
    }

    CloseServiceHandle(hSvc);
    return true;
}

void ServicesCollector::WriteServiceLine(std::wofstream& file, const ServiceInfo& info)
{
    file << info.name
         << L"  |  " << info.displayName
         << L"  |  State: " << StateToString(info.currentState)
         << L"  |  Start: " << StartTypeToString(info.startType);

    if (info.currentState == SERVICE_RUNNING && info.pid != 0)
        file << L"  |  PID: " << info.pid;

    if (!info.binaryPath.empty())
        file << L"  |  Path: " << info.binaryPath;
    if (!info.serviceAccount.empty())
        file << L"  |  Account: " << info.serviceAccount;

    file << std::endl;
}

bool ServicesCollector::Collect(const Config& config,
                                 const std::filesystem::path& outputDir,
                                 ProgressCallback cb,
                                 CancelToken cancel)
{
    if (cb) cb(Name(), L"Opening Service Control Manager...", 0);

    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!hSCM)
    {
        m_error = L"Failed to open Service Control Manager (error=" +
                  std::to_wstring(GetLastError()) + L")";
        return false;
    }

    // Enumerate all services
    DWORD bytesNeeded = 0, serviceCount = 0, resumeHandle = 0;
    EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                          SERVICE_STATE_ALL, nullptr, 0,
                          &bytesNeeded, &serviceCount, &resumeHandle, nullptr);

    if (bytesNeeded == 0)
    {
        CloseServiceHandle(hSCM);
        m_error = L"EnumServicesStatusExW returned 0 bytes needed";
        return false;
    }

    std::vector<BYTE> buffer(bytesNeeded);
    if (!EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                               SERVICE_STATE_ALL, buffer.data(),
                               static_cast<DWORD>(buffer.size()),
                               &bytesNeeded, &serviceCount, &resumeHandle, nullptr))
    {
        CloseServiceHandle(hSCM);
        m_error = L"EnumServicesStatusExW failed (error=" +
                  std::to_wstring(GetLastError()) + L")";
        return false;
    }

    auto* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());

    // Build list of ServiceInfo
    std::vector<ServiceInfo> allServices;
    allServices.reserve(serviceCount);

    if (cb) cb(Name(), L"Querying service details...", 10);

    for (DWORD i = 0; i < serviceCount; ++i)
    {
        if (cancel && cancel->load())
        {
            CloseServiceHandle(hSCM);
            m_error = L"Cancelled";
            return false;
        }

        ServiceInfo si;
        si.name = services[i].lpServiceName ? services[i].lpServiceName : L"";
        si.displayName = services[i].lpDisplayName ? services[i].lpDisplayName : L"";
        si.currentState = services[i].ServiceStatusProcess.dwCurrentState;
        si.pid = services[i].ServiceStatusProcess.dwProcessId;

        // Query additional details (start type, binary path, account)
        QueryServiceDetails(hSCM, si.name, si);
        allServices.push_back(std::move(si));
    }

    CloseServiceHandle(hSCM);

    // Sort alphabetically by name
    std::sort(allServices.begin(), allServices.end(),
              [](const ServiceInfo& a, const ServiceInfo& b) {
                  return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
              });

    // Write full services list
    if (cb) cb(Name(), L"Writing services.txt...", 60);

    std::filesystem::path outAll = outputDir / L"services.txt";
    std::wofstream fileAll(outAll);
    if (!fileAll.is_open())
    {
        m_error = L"Failed to create output file: " + outAll.wstring();
        return false;
    }

    fileAll << L"=== Windows Services ===" << std::endl;
    fileAll << L"Total services: " << allServices.size() << std::endl;
    fileAll << L"========================================" << std::endl << std::endl;

    for (const auto& si : allServices)
        WriteServiceLine(fileAll, si);

    fileAll.close();

    // Write product-specific services
    const auto& configSvcNames = config.GetServiceNames();
    if (!configSvcNames.empty())
    {
        if (cb) cb(Name(), L"Writing product_services.txt...", 80);

        std::filesystem::path outProduct = outputDir / L"product_services.txt";
        std::wofstream fileProd(outProduct);
        if (fileProd.is_open())
        {
            fileProd << L"=== Product Services ===" << std::endl;
            fileProd << L"Expected services: ";
            for (size_t i = 0; i < configSvcNames.size(); ++i)
            {
                if (i > 0) fileProd << L", ";
                fileProd << configSvcNames[i];
            }
            fileProd << std::endl;
            fileProd << L"========================================" << std::endl << std::endl;

            for (const auto& expectedName : configSvcNames)
            {
                bool found = false;
                for (const auto& si : allServices)
                {
                    if (StringUtils::EqualsIgnoreCase(si.name, expectedName))
                    {
                        WriteServiceLine(fileProd, si);

                        // Flag issues
                        if (si.currentState != SERVICE_RUNNING)
                            fileProd << L"  ** WARNING: Service is NOT running **" << std::endl;
                        if (si.startType == SERVICE_DISABLED)
                            fileProd << L"  ** WARNING: Service is DISABLED **" << std::endl;

                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    fileProd << expectedName << L"  |  ** NOT FOUND on this system **"
                             << std::endl;
                }
            }

            fileProd.close();
        }
    }

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[Services] Enumerated %zu services",
                           allServices.size());
    return true;
}

} // namespace KTDiag
