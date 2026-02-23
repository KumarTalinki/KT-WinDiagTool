#pragma once

#include "ICollector.h"

#include <Windows.h>
#include <fstream>

namespace KTDiag
{

class ServicesCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Windows Services"; }
    std::wstring Id() const override { return L"services"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    struct ServiceInfo
    {
        std::wstring name;
        std::wstring displayName;
        DWORD currentState = 0;
        DWORD startType = 0;
        DWORD pid = 0;
        std::wstring binaryPath;
        std::wstring serviceAccount;
    };

    // Query detailed config for a single service
    static bool QueryServiceDetails(SC_HANDLE hSCM, const std::wstring& serviceName,
                                    ServiceInfo& info);

    // Format service state as string
    static const wchar_t* StateToString(DWORD state);

    // Format start type as string
    static const wchar_t* StartTypeToString(DWORD startType);

    // Write a service info line to file
    static void WriteServiceLine(std::wofstream& file, const ServiceInfo& info);

    std::wstring m_error;
};

} // namespace KTDiag
