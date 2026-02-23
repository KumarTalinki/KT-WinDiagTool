#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace KTDiag
{

struct WppProvider
{
    std::wstring guid;
    std::wstring name;
    int level = 5;           // TRACE_LEVEL_VERBOSE
    std::wstring flags;      // Hex string e.g. "0xFFFF"
};

struct EtwProvider
{
    std::wstring guid;
    std::wstring name;
};

struct PacketCaptureConfig
{
    std::wstring interfaceName = L"auto";
    std::wstring filter;
    int durationSeconds = 60;
};

struct WppTraceLevels
{
    std::vector<std::wstring> available;
    std::wstring defaultLevel = L"Warning";
};

struct ProductLogLevels
{
    std::wstring levelRegistryKey;
    std::vector<std::wstring> available;
    std::wstring defaultLevel = L"Info";
};

class Config
{
public:
    Config();
    ~Config();

    // Load configuration from a JSON file
    bool LoadFromFile(const std::wstring& path);

    // Validate that the loaded config has required fields
    bool IsValid() const;

    // Getters
    const std::wstring& GetProductName() const { return m_productName; }
    const std::wstring& GetProductVersionRegistry() const { return m_productVersionRegistry; }
    const std::vector<std::wstring>& GetServiceNames() const { return m_serviceNames; }
    const std::vector<std::wstring>& GetLogPaths() const { return m_logPaths; }
    const std::vector<std::wstring>& GetInstallLogPaths() const { return m_installLogPaths; }
    const std::vector<WppProvider>& GetWppProviders() const { return m_wppProviders; }
    const std::vector<EtwProvider>& GetEtwProviders() const { return m_etwProviders; }
    const std::vector<std::wstring>& GetRegistryKeys() const { return m_registryKeys; }
    const std::vector<std::wstring>& GetEventLogChannels() const { return m_eventLogChannels; }
    const std::vector<std::wstring>& GetCrashDumpPaths() const { return m_crashDumpPaths; }
    const std::wstring& GetCrashDumpConfigRegistry() const { return m_crashDumpConfigRegistry; }
    const std::vector<std::wstring>& GetPerfCounters() const { return m_perfCounters; }
    const PacketCaptureConfig& GetPacketCaptureConfig() const { return m_packetCapture; }
    void SetPacketCaptureDuration(int seconds) { m_packetCapture.durationSeconds = seconds; }

    // Duration for timed live-capture collectors (ETW, WPP). Default: 30 s.
    // --duration CLI flag overrides this for all timed collectors.
    int  GetCaptureDurationSeconds() const { return m_captureDurationSeconds; }
    void SetCaptureDurationSeconds(int seconds) { m_captureDurationSeconds = seconds; }
    const WppTraceLevels& GetWppTraceLevels() const { return m_wppTraceLevels; }
    const ProductLogLevels& GetProductLogLevels() const { return m_productLogLevels; }

    // Expand environment variables in a path (e.g., %ProgramData% -> C:\ProgramData)
    static std::wstring ExpandEnvironmentPath(const std::wstring& path);

private:
    std::wstring m_productName;
    std::wstring m_productVersionRegistry;
    std::vector<std::wstring> m_serviceNames;
    std::vector<std::wstring> m_logPaths;
    std::vector<std::wstring> m_installLogPaths;
    std::vector<WppProvider> m_wppProviders;
    std::vector<EtwProvider> m_etwProviders;
    std::vector<std::wstring> m_registryKeys;
    std::vector<std::wstring> m_eventLogChannels;
    std::vector<std::wstring> m_crashDumpPaths;
    std::wstring m_crashDumpConfigRegistry;
    std::vector<std::wstring> m_perfCounters;
    PacketCaptureConfig m_packetCapture;
    WppTraceLevels m_wppTraceLevels;
    ProductLogLevels m_productLogLevels;
    int m_captureDurationSeconds = 30;
};

} // namespace KTDiag
