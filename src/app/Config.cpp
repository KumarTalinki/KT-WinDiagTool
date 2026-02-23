#include "Config.h"
#include "../util/Logger.h"
#include "../util/StringUtils.h"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <fstream>

using json = nlohmann::json;

namespace KTDiag
{

Config::Config() = default;
Config::~Config() = default;

static std::vector<std::wstring> GetStringArray(const json& j, const std::string& key)
{
    std::vector<std::wstring> result;
    if (j.contains(key) && j[key].is_array())
    {
        for (const auto& item : j[key])
        {
            if (item.is_string())
                result.push_back(StringUtils::Utf8ToWide(item.get<std::string>()));
        }
    }
    return result;
}

static std::wstring GetString(const json& j, const std::string& key,
                              const std::wstring& defaultVal = L"")
{
    if (j.contains(key) && j[key].is_string())
        return StringUtils::Utf8ToWide(j[key].get<std::string>());
    return defaultVal;
}

static int GetInt(const json& j, const std::string& key, int defaultVal = 0)
{
    if (j.contains(key) && j[key].is_number_integer())
        return j[key].get<int>();
    return defaultVal;
}

bool Config::LoadFromFile(const std::wstring& path)
{
    try
    {
        std::string narrowPath = StringUtils::WideToUtf8(path);
        std::ifstream file(narrowPath);
        if (!file.is_open())
        {
            Logger::Instance().Log(LogLevel::Error, L"Cannot open config file: %s", path.c_str());
            return false;
        }

        json j = json::parse(file, nullptr, true, true);  // allow comments

        // Required fields
        m_productName = GetString(j, "product_name");
        if (m_productName.empty())
        {
            Logger::Instance().Log(LogLevel::Error, L"Config missing required field: product_name");
            return false;
        }

        // Optional fields with defaults
        m_productVersionRegistry = GetString(j, "product_version_registry");
        m_serviceNames = GetStringArray(j, "service_names");
        m_logPaths = GetStringArray(j, "log_paths");
        m_installLogPaths = GetStringArray(j, "install_log_paths");
        m_registryKeys = GetStringArray(j, "registry_keys");
        m_eventLogChannels = GetStringArray(j, "event_log_channels");
        m_crashDumpPaths = GetStringArray(j, "crash_dump_paths");
        m_crashDumpConfigRegistry = GetString(j, "crash_dump_config_registry");
        m_perfCounters = GetStringArray(j, "perf_counters");

        // Default event log channels if none specified
        if (m_eventLogChannels.empty())
        {
            m_eventLogChannels.push_back(L"Application");
            m_eventLogChannels.push_back(L"System");
        }

        // WPP providers
        if (j.contains("wpp_providers") && j["wpp_providers"].is_array())
        {
            for (const auto& p : j["wpp_providers"])
            {
                WppProvider provider;
                provider.guid = GetString(p, "guid");
                provider.name = GetString(p, "name");
                provider.level = GetInt(p, "level", 5);
                provider.flags = GetString(p, "flags", L"0xFFFF");
                if (!provider.guid.empty())
                    m_wppProviders.push_back(std::move(provider));
            }
        }

        // ETW providers
        if (j.contains("etw_providers") && j["etw_providers"].is_array())
        {
            for (const auto& p : j["etw_providers"])
            {
                EtwProvider provider;
                provider.guid = GetString(p, "guid");
                provider.name = GetString(p, "name");
                if (!provider.guid.empty())
                    m_etwProviders.push_back(std::move(provider));
            }
        }

        // Packet capture config
        if (j.contains("packet_capture") && j["packet_capture"].is_object())
        {
            const auto& pc = j["packet_capture"];
            m_packetCapture.interfaceName = GetString(pc, "interface", L"auto");
            m_packetCapture.filter = GetString(pc, "filter");
            m_packetCapture.durationSeconds = GetInt(pc, "duration_seconds", 60);
        }

        // WPP trace levels
        if (j.contains("wpp_trace_levels") && j["wpp_trace_levels"].is_object())
        {
            const auto& tl = j["wpp_trace_levels"];
            m_wppTraceLevels.available = GetStringArray(tl, "available");
            m_wppTraceLevels.defaultLevel = GetString(tl, "default", L"Warning");
        }
        if (m_wppTraceLevels.available.empty())
        {
            m_wppTraceLevels.available = { L"Critical", L"Error", L"Warning", L"Info", L"Verbose" };
        }

        // Product log levels
        if (j.contains("product_log_levels") && j["product_log_levels"].is_object())
        {
            const auto& pl = j["product_log_levels"];
            m_productLogLevels.levelRegistryKey = GetString(pl, "level_registry_key");
            m_productLogLevels.available = GetStringArray(pl, "available");
            m_productLogLevels.defaultLevel = GetString(pl, "default", L"Info");
        }

        // Expand environment variables in all paths
        for (auto& p : m_logPaths) p = ExpandEnvironmentPath(p);
        for (auto& p : m_installLogPaths) p = ExpandEnvironmentPath(p);
        for (auto& p : m_crashDumpPaths) p = ExpandEnvironmentPath(p);

        Logger::Instance().Log(LogLevel::Info, L"Loaded config for product: %s", m_productName.c_str());
        return true;
    }
    catch (const json::parse_error& e)
    {
        std::wstring msg = StringUtils::Utf8ToWide(e.what());
        Logger::Instance().Log(LogLevel::Error, L"JSON parse error: %s", msg.c_str());
        return false;
    }
    catch (const std::exception& e)
    {
        std::wstring msg = StringUtils::Utf8ToWide(e.what());
        Logger::Instance().Log(LogLevel::Error, L"Config load error: %s", msg.c_str());
        return false;
    }
}

bool Config::IsValid() const
{
    return !m_productName.empty();
}

std::wstring Config::ExpandEnvironmentPath(const std::wstring& path)
{
    wchar_t expanded[MAX_PATH * 2] = {};
    DWORD len = ExpandEnvironmentStringsW(path.c_str(), expanded, _countof(expanded));
    if (len > 0 && len < _countof(expanded))
        return std::wstring(expanded);
    return path;
}

} // namespace KTDiag
