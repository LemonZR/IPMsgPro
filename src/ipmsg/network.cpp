// ============================================================================
// Network Utility Functions Implementation
// ============================================================================

// WinSock2 must come before Windows.h (included via network.h)
#include "network.h"
#include "msgmng.h"  // for MsgLog
#include <Windows.h>
#include <Lmcons.h>  // UNLEN
#include <regex>

namespace ipmsg {

bool WSAInit() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        return false;
    }
    // Confirm WinSock 2.2
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        WSACleanup();
        return false;
    }
    return true;
}

void WSACleanup() {
    ::WSACleanup();
}

std::vector<std::string> GetLocalIPAddresses() {
    std::vector<std::string> addresses;

    ULONG bufLen = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                         GAA_FLAG_SKIP_DNS_SERVER, nullptr, nullptr, &bufLen);
    if (bufLen == 0) {
        MsgLog("[Network] GetAdaptersAddresses returned zero buffer length");
        return addresses;
    }

    std::vector<uint8_t> buffer(bufLen);
    auto adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    ULONG ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                     GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &bufLen);
    if (ret != ERROR_SUCCESS) {
        MsgLog("[Network] GetAdaptersAddresses failed, error=" + std::to_string(ret));
        return addresses;
    }

    MsgLog("[Network] Scanning network adapters...");
    for (auto adapter = adapters; adapter; adapter = adapter->Next) {
        std::string adapterName = adapter->AdapterName ? adapter->AdapterName : "(unknown)";
        MsgLog("[Network] Adapter: " + adapterName +
               ", Status=" + (adapter->OperStatus == IfOperStatusUp ? "UP" : "DOWN") +
               ", Type=" + std::to_string(adapter->IfType));

        if (adapter->OperStatus != IfOperStatusUp) continue;
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            MsgLog("[Network] Skipping loopback adapter");
            continue;
        }

        for (auto addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
            if (addr->Address.lpSockaddr->sa_family != AF_INET) continue;

            auto sa = reinterpret_cast<sockaddr_in*>(addr->Address.lpSockaddr);
            char ipStr[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &sa->sin_addr, ipStr, sizeof(ipStr));
            addresses.push_back(ipStr);
            MsgLog("[Network] Found local IP: " + std::string(ipStr));
        }
    }

    MsgLog("[Network] Total local IPs found: " + std::to_string(addresses.size()));
    return addresses;
}

std::string GetBroadcastAddress(const std::string& ip) {
    uint32_t ipVal = IPToUint32(ip);
    if (ipVal == 0) return "";

    // Convert to host byte order for calculations
    uint32_t ipHost = ntohl(ipVal);
    
    // Simple heuristic: assume /24 subnet for common home/office networks
    // 255.255.255.0 in host byte order
    uint32_t mask = 0xFFFFFF00;
    uint32_t network = ipHost & mask;
    uint32_t broadcast = network | 0x000000FF;

    // Convert back to network byte order for output
    return Uint32ToIP(htonl(broadcast));
}

std::vector<std::string> GetAllBroadcastAddresses() {
    std::vector<std::string> broadcasts;
    broadcasts.push_back("255.255.255.255");
    MsgLog("[Network] Using limited broadcast address only: 255.255.255.255");
    return broadcasts;
}

uint32_t IPToUint32(const std::string& ip) {
    uint32_t result = 0;
    inet_pton(AF_INET, ip.c_str(), &result);
    return result;
}

std::string Uint32ToIP(uint32_t ip) {
    char buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &ip, buf, sizeof(buf));
    return buf;
}

bool IsInSubnet(const std::string& ip, const std::string& subnet) {
    // Parse CIDR: "192.168.1.0/24"
    size_t slashPos = subnet.find('/');
    if (slashPos == std::string::npos) return ip == subnet;

    std::string netPart = subnet.substr(0, slashPos);
    int prefixLen = std::stoi(subnet.substr(slashPos + 1));

    uint32_t ipVal = IPToUint32(ip);
    uint32_t netVal = IPToUint32(netPart);

    if (prefixLen == 0) return true;
    uint32_t mask = (prefixLen >= 32) ? 0xFFFFFFFF : (0xFFFFFFFF << (32 - prefixLen));

    return (ipVal & mask) == (netVal & mask);
}

std::string GetHostName() {
    char buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = sizeof(buf);
    GetComputerNameA(buf, &size);
    return buf;
}

std::string GetUserName() {
    char buf[UNLEN + 1] = {};
    DWORD size = sizeof(buf);
    ::GetUserNameA(buf, &size);
    return buf;
}

} // namespace ipmsg
