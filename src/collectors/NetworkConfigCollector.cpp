// Require Windows Vista+ APIs (GetAdaptersAddresses, GetExtendedTcpTable, etc.)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

// winsock2.h MUST be included before Windows.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>

#include "NetworkConfigCollector.h"
#include "../util/Logger.h"

#include <vector>
#include <sstream>
#include <iomanip>
#include <fstream>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace KTDiag
{

static std::wstring Ipv4ToString(DWORD addr)
{
    wchar_t buf[64] = {};
    in_addr in;
    in.S_un.S_addr = addr;
    InetNtopW(AF_INET, &in, buf, _countof(buf));
    return buf;
}

static std::wstring SockAddrToString(LPSOCKADDR sa)
{
    if (!sa) return L"";
    wchar_t buf[256] = {};

    if (sa->sa_family == AF_INET)
    {
        auto* sin = reinterpret_cast<sockaddr_in*>(sa);
        InetNtopW(AF_INET, &sin->sin_addr, buf, _countof(buf));
    }
    else if (sa->sa_family == AF_INET6)
    {
        auto* sin6 = reinterpret_cast<sockaddr_in6*>(sa);
        InetNtopW(AF_INET6, &sin6->sin6_addr, buf, _countof(buf));
    }
    return buf;
}

static std::wstring FormatMacAddress(const BYTE* addr, ULONG len)
{
    if (len == 0) return L"(none)";
    std::wostringstream oss;
    for (ULONG i = 0; i < len; ++i)
    {
        if (i > 0) oss << L'-';
        oss << std::hex << std::uppercase << std::setfill(L'0') << std::setw(2) << addr[i];
    }
    return oss.str();
}

static const wchar_t* AdapterTypeToString(DWORD ifType)
{
    switch (ifType)
    {
    case IF_TYPE_ETHERNET_CSMACD:   return L"Ethernet";
    case IF_TYPE_IEEE80211:         return L"Wi-Fi";
    case IF_TYPE_TUNNEL:            return L"Tunnel";
    case IF_TYPE_PPP:               return L"PPP";
    case IF_TYPE_SOFTWARE_LOOPBACK: return L"Loopback";
    default:                        return L"Other";
    }
}

static const wchar_t* OperStatusToString(IF_OPER_STATUS status)
{
    switch (status)
    {
    case IfOperStatusUp:             return L"Up";
    case IfOperStatusDown:           return L"Down";
    case IfOperStatusTesting:        return L"Testing";
    case IfOperStatusDormant:        return L"Dormant";
    case IfOperStatusNotPresent:     return L"Not Present";
    case IfOperStatusLowerLayerDown: return L"Lower Layer Down";
    default:                         return L"Unknown";
    }
}

bool NetworkConfigCollector::CollectAdapters(const std::filesystem::path& outputDir,
                                              CancelToken cancel)
{
    std::filesystem::path outFile = outputDir / L"network_config.txt";
    std::wofstream file(outFile);
    if (!file.is_open())
    {
        m_error = L"Failed to create output file: " + outFile.wstring();
        return false;
    }

    file << L"=== Network Configuration ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    // Get adapter addresses
    // GAA_FLAG_INCLUDE_PREFIX = 0x0010, GAA_FLAG_INCLUDE_GATEWAYS = 0x0080
    ULONG flags = 0x0010 | 0x0080;
    ULONG bufSize = 0;
    GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &bufSize);
    if (bufSize == 0)
    {
        file << L"No network adapters found." << std::endl;
        file.close();
        return true;
    }

    std::vector<BYTE> buffer(bufSize);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &bufSize);
    if (rc != NO_ERROR)
    {
        file << L"GetAdaptersAddresses failed (error=" << rc << L")" << std::endl;
        file.close();
        return true;  // Non-fatal
    }

    int adapterCount = 0;
    for (auto* adapter = adapters; adapter; adapter = adapter->Next)
    {
        if (cancel && cancel->load()) break;
        ++adapterCount;

        file << L"--- Adapter: " << adapter->FriendlyName << L" ---" << std::endl;
        file << L"  Description:  " << adapter->Description << std::endl;
        file << L"  Type:         " << AdapterTypeToString(adapter->IfType) << std::endl;
        file << L"  Status:       " << OperStatusToString(adapter->OperStatus) << std::endl;
        file << L"  MAC Address:  "
             << FormatMacAddress(adapter->PhysicalAddress, adapter->PhysicalAddressLength)
             << std::endl;

        // DHCP
        if (adapter->Flags & IP_ADAPTER_DHCP_ENABLED)
            file << L"  DHCP:         Enabled" << std::endl;
        else
            file << L"  DHCP:         Disabled" << std::endl;

        // DNS suffix
        if (adapter->DnsSuffix && wcslen(adapter->DnsSuffix) > 0)
            file << L"  DNS Suffix:   " << adapter->DnsSuffix << std::endl;

        // Unicast addresses (IPv4 and IPv6)
        for (auto* ua = adapter->FirstUnicastAddress; ua; ua = ua->Next)
        {
            std::wstring addr = SockAddrToString(ua->Address.lpSockaddr);
            if (!addr.empty())
            {
                file << L"  IP Address:   " << addr
                     << L" /" << static_cast<int>(ua->OnLinkPrefixLength) << std::endl;
            }
        }

        // Gateways
        for (auto* gw = adapter->FirstGatewayAddress; gw; gw = gw->Next)
        {
            std::wstring addr = SockAddrToString(gw->Address.lpSockaddr);
            if (!addr.empty())
                file << L"  Gateway:      " << addr << std::endl;
        }

        // DNS servers
        for (auto* dns = adapter->FirstDnsServerAddress; dns; dns = dns->Next)
        {
            std::wstring addr = SockAddrToString(dns->Address.lpSockaddr);
            if (!addr.empty())
                file << L"  DNS Server:   " << addr << std::endl;
        }

        file << std::endl;
    }

    // DNS suffix search list from registry
    file << L"--- DNS Configuration ---" << std::endl;
    HKEY hTcpip = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
        0, KEY_READ, &hTcpip) == ERROR_SUCCESS)
    {
        // Domain
        DWORD dataSize = 0;
        DWORD type = 0;
        wchar_t domBuf[512] = {};
        dataSize = sizeof(domBuf);
        if (RegQueryValueExW(hTcpip, L"Domain", nullptr, &type,
                             reinterpret_cast<BYTE*>(domBuf), &dataSize) == ERROR_SUCCESS)
        {
            file << L"  Primary DNS Domain: " << domBuf << std::endl;
        }

        // Search list
        wchar_t searchBuf[2048] = {};
        dataSize = sizeof(searchBuf);
        if (RegQueryValueExW(hTcpip, L"SearchList", nullptr, &type,
                             reinterpret_cast<BYTE*>(searchBuf), &dataSize) == ERROR_SUCCESS)
        {
            file << L"  DNS Search List:    " << searchBuf << std::endl;
        }

        RegCloseKey(hTcpip);
    }

    // Network profiles
    file << std::endl << L"--- Network Profiles ---" << std::endl;
    HKEY hProfiles = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList\\Profiles",
        0, KEY_READ, &hProfiles) == ERROR_SUCCESS)
    {
        DWORD subKeyCount = 0;
        RegQueryInfoKeyW(hProfiles, nullptr, nullptr, nullptr, &subKeyCount,
                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        for (DWORD i = 0; i < subKeyCount && i < 50; ++i)
        {
            wchar_t keyName[256] = {};
            DWORD keyNameLen = _countof(keyName);
            if (RegEnumKeyExW(hProfiles, i, keyName, &keyNameLen,
                              nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                continue;

            HKEY hProfile = nullptr;
            if (RegOpenKeyExW(hProfiles, keyName, 0, KEY_READ, &hProfile) != ERROR_SUCCESS)
                continue;

            wchar_t profileName[256] = {};
            DWORD nameSize = sizeof(profileName);
            RegQueryValueExW(hProfile, L"ProfileName", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(profileName), &nameSize);

            DWORD category = 0;
            DWORD catSize = sizeof(category);
            RegQueryValueExW(hProfile, L"Category", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&category), &catSize);

            const wchar_t* catStr = L"Unknown";
            switch (category)
            {
            case 0: catStr = L"Public"; break;
            case 1: catStr = L"Private"; break;
            case 2: catStr = L"Domain"; break;
            }

            file << L"  " << profileName << L"  (" << catStr << L")" << std::endl;
            RegCloseKey(hProfile);
        }

        RegCloseKey(hProfiles);
    }

    file << std::endl;
    file << L"========================================" << std::endl;
    file << L"Total adapters: " << adapterCount << std::endl;
    file.close();

    Logger::Instance().Log(LogLevel::Info, L"[Network] Found %d adapters", adapterCount);
    return true;
}

bool NetworkConfigCollector::CollectRoutingTable(const std::filesystem::path& outputDir)
{
    std::filesystem::path outFile = outputDir / L"routing_table.txt";
    std::wofstream file(outFile);
    if (!file.is_open()) return true;  // Non-fatal

    file << L"=== IPv4 Routing Table ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    ULONG bufSize = 0;
    GetIpForwardTable(nullptr, &bufSize, TRUE);
    if (bufSize == 0)
    {
        file << L"No routes found." << std::endl;
        file.close();
        return true;
    }

    std::vector<BYTE> buffer(bufSize);
    auto* table = reinterpret_cast<MIB_IPFORWARDTABLE*>(buffer.data());
    if (GetIpForwardTable(table, &bufSize, TRUE) != NO_ERROR)
    {
        file << L"GetIpForwardTable failed." << std::endl;
        file.close();
        return true;
    }

    file << std::left
         << std::setw(20) << L"Destination"
         << std::setw(20) << L"Netmask"
         << std::setw(20) << L"Gateway"
         << std::setw(10) << L"Metric"
         << std::setw(10) << L"IfIndex"
         << std::endl;
    file << std::wstring(80, L'-') << std::endl;

    for (DWORD i = 0; i < table->dwNumEntries; ++i)
    {
        const auto& row = table->table[i];
        file << std::left
             << std::setw(20) << Ipv4ToString(row.dwForwardDest)
             << std::setw(20) << Ipv4ToString(row.dwForwardMask)
             << std::setw(20) << Ipv4ToString(row.dwForwardNextHop)
             << std::setw(10) << row.dwForwardMetric1
             << std::setw(10) << row.dwForwardIfIndex
             << std::endl;
    }

    file << std::endl << L"Total routes: " << table->dwNumEntries << std::endl;
    file.close();
    return true;
}

bool NetworkConfigCollector::CollectActiveConnections(const std::filesystem::path& outputDir,
                                                       CancelToken cancel)
{
    std::filesystem::path outFile = outputDir / L"active_connections.txt";
    std::wofstream file(outFile);
    if (!file.is_open()) return true;  // Non-fatal

    file << L"=== Active Network Connections ===" << std::endl;
    file << L"========================================" << std::endl << std::endl;

    // TCP connections
    file << L"--- TCP Connections ---" << std::endl;
    file << std::left
         << std::setw(8)  << L"Proto"
         << std::setw(25) << L"Local Address"
         << std::setw(25) << L"Remote Address"
         << std::setw(16) << L"State"
         << std::setw(8)  << L"PID"
         << std::endl;
    file << std::wstring(82, L'-') << std::endl;

    ULONG tcpBufSize = 0;
    GetExtendedTcpTable(nullptr, &tcpBufSize, TRUE, AF_INET,
                        TCP_TABLE_OWNER_PID_ALL, 0);
    if (tcpBufSize > 0)
    {
        std::vector<BYTE> tcpBuf(tcpBufSize);
        if (GetExtendedTcpTable(tcpBuf.data(), &tcpBufSize, TRUE, AF_INET,
                                TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR)
        {
            auto* tcpTable = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(tcpBuf.data());
            for (DWORD i = 0; i < tcpTable->dwNumEntries; ++i)
            {
                if (cancel && cancel->load()) break;
                const auto& row = tcpTable->table[i];

                std::wstring localAddr = Ipv4ToString(row.dwLocalAddr) + L":" +
                                         std::to_wstring(ntohs(static_cast<u_short>(row.dwLocalPort)));
                std::wstring remoteAddr = Ipv4ToString(row.dwRemoteAddr) + L":" +
                                          std::to_wstring(ntohs(static_cast<u_short>(row.dwRemotePort)));

                const wchar_t* state = L"?";
                switch (row.dwState)
                {
                case MIB_TCP_STATE_CLOSED:     state = L"CLOSED"; break;
                case MIB_TCP_STATE_LISTEN:     state = L"LISTEN"; break;
                case MIB_TCP_STATE_SYN_SENT:   state = L"SYN_SENT"; break;
                case MIB_TCP_STATE_SYN_RCVD:   state = L"SYN_RCVD"; break;
                case MIB_TCP_STATE_ESTAB:      state = L"ESTABLISHED"; break;
                case MIB_TCP_STATE_FIN_WAIT1:  state = L"FIN_WAIT1"; break;
                case MIB_TCP_STATE_FIN_WAIT2:  state = L"FIN_WAIT2"; break;
                case MIB_TCP_STATE_CLOSE_WAIT: state = L"CLOSE_WAIT"; break;
                case MIB_TCP_STATE_CLOSING:    state = L"CLOSING"; break;
                case MIB_TCP_STATE_LAST_ACK:   state = L"LAST_ACK"; break;
                case MIB_TCP_STATE_TIME_WAIT:  state = L"TIME_WAIT"; break;
                }

                file << std::left
                     << std::setw(8)  << L"TCP"
                     << std::setw(25) << localAddr
                     << std::setw(25) << remoteAddr
                     << std::setw(16) << state
                     << std::setw(8)  << row.dwOwningPid
                     << std::endl;
            }
            file << std::endl << L"TCP connections: " << tcpTable->dwNumEntries << std::endl;
        }
    }

    file << std::endl;

    // UDP endpoints
    file << L"--- UDP Endpoints ---" << std::endl;
    file << std::left
         << std::setw(8)  << L"Proto"
         << std::setw(25) << L"Local Address"
         << std::setw(8)  << L"PID"
         << std::endl;
    file << std::wstring(41, L'-') << std::endl;

    ULONG udpBufSize = 0;
    GetExtendedUdpTable(nullptr, &udpBufSize, TRUE, AF_INET,
                        UDP_TABLE_OWNER_PID, 0);
    if (udpBufSize > 0)
    {
        std::vector<BYTE> udpBuf(udpBufSize);
        if (GetExtendedUdpTable(udpBuf.data(), &udpBufSize, TRUE, AF_INET,
                                UDP_TABLE_OWNER_PID, 0) == NO_ERROR)
        {
            auto* udpTable = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(udpBuf.data());
            for (DWORD i = 0; i < udpTable->dwNumEntries; ++i)
            {
                if (cancel && cancel->load()) break;
                const auto& row = udpTable->table[i];

                std::wstring localAddr = Ipv4ToString(row.dwLocalAddr) + L":" +
                                         std::to_wstring(ntohs(static_cast<u_short>(row.dwLocalPort)));

                file << std::left
                     << std::setw(8)  << L"UDP"
                     << std::setw(25) << localAddr
                     << std::setw(8)  << row.dwOwningPid
                     << std::endl;
            }
            file << std::endl << L"UDP endpoints: " << udpTable->dwNumEntries << std::endl;
        }
    }

    file.close();
    return true;
}

bool NetworkConfigCollector::Collect(const Config& config,
                                      const std::filesystem::path& outputDir,
                                      ProgressCallback cb,
                                      CancelToken cancel)
{
    UNREFERENCED_PARAMETER(config);
    if (cb) cb(Name(), L"Collecting network adapter info...", 0);

    if (!CollectAdapters(outputDir, cancel))
        return false;

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Collecting routing table...", 40);
    CollectRoutingTable(outputDir);

    if (cancel && cancel->load()) { m_error = L"Cancelled"; return false; }
    if (cb) cb(Name(), L"Collecting active connections...", 70);
    CollectActiveConnections(outputDir, cancel);

    if (cb) cb(Name(), L"Done", 100);
    Logger::Instance().Log(LogLevel::Info, L"[Network] Network configuration collected");
    return true;
}

} // namespace KTDiag
