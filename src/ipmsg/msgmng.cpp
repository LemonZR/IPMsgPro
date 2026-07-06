// ============================================================================
// IPMsg Message Manager Implementation
// ============================================================================

#include "msgmng.h"
#include <algorithm>
#include <sstream>
#include <chrono>
#include <iostream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <thread>
#include <iomanip>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <Windows.h>
#endif

namespace ipmsg {

// ============================================================================
// Logger (shared with main.cpp pattern)
// ============================================================================
static std::ofstream s_logFile;
static std::mutex s_logMutex;
static bool s_logInitialized = false;
static bool s_logEnabled =
#ifdef _DEBUG
    true;   // Debug build: log enabled by default
#else
    false;  // Release build: log disabled by default
#endif

static void EnsureLogInit() {
    if (s_logInitialized) return;
    s_logInitialized = true;
    
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir = std::string(exePath);
    dir = dir.substr(0, dir.find_last_of('\\')) + "\\IPMsgPro";
    CreateDirectoryA(dir.c_str(), nullptr);
    s_logFile.open(dir + "\\ipmsgpro.log", std::ios::app);
}

static std::string GetLogTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void MsgLog(const std::string& msg) {
    if (!s_logEnabled) return;
    std::lock_guard<std::mutex> lock(s_logMutex);
    EnsureLogInit();
    std::string logMsg = "[" + GetLogTimestamp() + "] " + msg + "\n";
    if (s_logFile.is_open()) {
        s_logFile << logMsg;
        s_logFile.flush();
    }
    std::cout << logMsg;
}

void SetLogEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_logEnabled = enabled;
    if (enabled) EnsureLogInit();
}

bool IsLogEnabled() {
    return s_logEnabled;
}

// ============================================================================
// Encoding Conversion (GBK <-> UTF-8)
// ============================================================================

// Check if a string is valid UTF-8
static bool IsValidUTF8(const std::string& str) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(str.c_str());
    size_t len = str.length();
    size_t i = 0;
    
    while (i < len) {
        unsigned char b = bytes[i];
        int bytes_needed = 0;
        
        if (b < 0x80) {
            bytes_needed = 1;
        } else if ((b & 0xE0) == 0xC0) {
            bytes_needed = 2;
        } else if ((b & 0xF0) == 0xE0) {
            bytes_needed = 3;
        } else if ((b & 0xF8) == 0xF0) {
            bytes_needed = 4;
        } else {
            return false; // Invalid UTF-8 start byte
        }
        
        if (i + bytes_needed > len) return false;
        
        for (int j = 1; j < bytes_needed; ++j) {
            if ((bytes[i + j] & 0xC0) != 0x80) return false;
        }
        
        i += bytes_needed;
    }
    
    return true;
}

// Convert GBK to UTF-8 using Windows API
static std::string GBKToUTF8(const std::string& gbkStr) {
#ifdef _WIN32
    if (gbkStr.empty()) return gbkStr;
    
    // GBK -> UTF-16
    int wlen = MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return gbkStr;
    
    std::wstring wstr(wlen - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), -1, &wstr[0], wlen);
    
    // UTF-16 -> UTF-8
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return gbkStr;
    
    std::string utf8Str(ulen - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8Str[0], ulen, nullptr, nullptr);
    
    return utf8Str;
#else
    return gbkStr; // Fallback: return as-is
#endif
}

// Convert UTF-8 to GBK using Windows API
static std::string UTF8ToGBK(const std::string& utf8Str) {
#ifdef _WIN32
    if (utf8Str.empty()) return utf8Str;
    
    // UTF-8 -> UTF-16
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return utf8Str;
    
    std::wstring wstr(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &wstr[0], wlen);
    
    // UTF-16 -> GBK
    int glen = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (glen <= 0) return utf8Str;
    
    std::string gbkStr(glen - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &gbkStr[0], glen, nullptr, nullptr);
    
    return gbkStr;
#else
    return utf8Str;
#endif
}

// Ensure a string is UTF-8 (convert from GBK if needed)
static std::string EnsureUTF8(const std::string& str) {
    if (str.empty()) return str;
    if (IsValidUTF8(str)) return str;
    // Not valid UTF-8, assume GBK and convert
    return GBKToUTF8(str);
}

MsgMng::MsgMng() = default;

MsgMng::~MsgMng() {
    Shutdown();
}

bool MsgMng::Init(int portNo, const std::string& userName,
                  const std::string& hostName, const std::string& nickName) {
    if (ready_) return true;

    portNo_ = portNo;

    // Setup local user info
    // GetUserName() / GetHostName() return GBK (system encoding on Windows).
    // Convert to UTF-8 so localUser_ is always stored in UTF-8 internally.
    localUser_.userName = userName.empty() ? EnsureUTF8(GetUserName()) : userName;
    localUser_.hostName = hostName.empty() ? EnsureUTF8(GetHostName()) : hostName;
    localUser_.nickName = nickName.empty() ? localUser_.userName : nickName;
    localUser_.portNo = portNo_;
    localUser_.active = true;
    localUser_.updateTime = std::time(nullptr);

    MsgLog("[MsgMng] Local user: " + localUser_.userName + "@" + localUser_.hostName +
           ", nickname=" + localUser_.nickName + ", port=" + std::to_string(portNo_));

    // Auto-detect local IPs
    auto localIPs = GetLocalIPAddresses();
    if (!localIPs.empty()) {
        localUser_.ipAddress = localIPs[0];
        MsgLog("[MsgMng] Using primary IP: " + localUser_.ipAddress);
    } else {
        MsgLog("[MsgMng] WARNING: No local IP addresses found!");
    }

    // Create UDP socket
    if (!CreateUdpSocket()) {
        MsgLog("[MsgMng] ERROR: Failed to create UDP socket!");
        return false;
    }
    MsgLog("[MsgMng] UDP socket created and bound to port " + std::to_string(portNo_));

    // Auto-detect broadcast segments (used only for legacy AddSegment interface)
    // Note: UdpBroadcast now only uses GetAllBroadcastAddresses() directly,
    // not segments_. IP range scanning uses ScanIpRange/ScanIpRanges.

    // Start receive thread
    running_ = true;
    recvThread_ = std::thread(&MsgMng::ReceiveThreadFunc, this);
    MsgLog("[MsgMng] Receive thread started");

    ready_ = true;
    return true;
}

void MsgMng::Shutdown() {
    if (!ready_) return;

    // Broadcast exit
    if (udpSock_ != INVALID_SOCKET) {
        BroadcastExit();
    }

    running_ = false;

    // Close socket to unblock recv
    if (udpSock_ != INVALID_SOCKET) {
        closesocket(udpSock_);
        udpSock_ = INVALID_SOCKET;
    }

    if (recvThread_.joinable()) {
        recvThread_.join();
    }

    ready_ = false;
}

bool MsgMng::CreateUdpSocket() {
    udpSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSock_ == INVALID_SOCKET) {
        return false;
    }

    // Allow broadcast
    BOOL bBroadcast = TRUE;
    setsockopt(udpSock_, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&bBroadcast), sizeof(bBroadcast));

    // Allow address reuse
    BOOL bReuse = TRUE;
    setsockopt(udpSock_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&bReuse), sizeof(bReuse));

    // Bind
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(portNo_));

    if (bind(udpSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(udpSock_);
        udpSock_ = INVALID_SOCKET;
        return false;
    }

    // Set recv buffer size
    int bufSize = MAX_SOCKBUF;
    setsockopt(udpSock_, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

    return true;
}

void MsgMng::ReceiveThreadFunc() {
    char buf[MAX_UDPBUF];
    sockaddr_in fromAddr = {};
    int fromLen = sizeof(fromAddr);

    while (running_) {
        int recvLen = recvfrom(udpSock_, buf, sizeof(buf) - 1, 0,
                               reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);
        if (recvLen <= 0) {
            if (!running_) break;
            continue;
        }

        buf[recvLen] = '\0';

        char fromIP[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &fromAddr.sin_addr, fromIP, sizeof(fromIP));
        int fromPort = ntohs(fromAddr.sin_port);

        ProcessRecvBuffer(fromAddr, buf, recvLen);
    }
}

void MsgMng::ProcessRecvBuffer(const sockaddr_in& fromAddr, const char* data, int len) {
    char fromIP[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &fromAddr.sin_addr, fromIP, sizeof(fromIP));
    int fromPort = ntohs(fromAddr.sin_port);

    MsgBuf msg;
    if (!ResolveMsg(data, len, fromIP, fromPort, msg)) {
        return;
    }

    // Skip own messages.
    // A message is "own" only if it has the same key AND the same port.
    // Same user@host on a different port means it's a different instance (e.g. two IPMsgPro instances).
    if (msg.sender.Key() == localUser_.Key() && msg.sender.portNo == localUser_.portNo) {
        return;
    }

    // Fix: replace UDP ephemeral port with known listening port if user is already in our list.
    // The msg.sender.portNo is set from the UDP recvfrom source port (ephemeral).
    // For RECVMSG replies, ANSENTRY, SENDMSG and file transfers, we need the sender's actual listening port.
    {
        auto knownUser = FindUser(msg.sender.Key());
        if (knownUser && knownUser->portNo != msg.sender.portNo) {
            msg.sender.portNo = knownUser->portNo;
        } else if (!knownUser && msg.sender.ipAddress == "127.0.0.1") {
            // When running two instances on the same machine, the hostName
            // ("localhost") may differ from the actual hostName broadcast by the other instance.
            // Try matching by IP address to find the correct listening port.
            auto users = GetUsers();  // GetUsers is always const
            for (const auto& u : users) {
                if (u.ipAddress == "127.0.0.1" && u.portNo != msg.sender.portNo &&
                    u.portNo != portNo_ && u.portNo > 0) {
                    msg.sender.portNo = u.portNo;
                    break;
                }
            }
        }
    }

    uint32_t mode = GET_MODE(msg.command);

    switch (mode) {
    case IPMSG_BR_ENTRY: {
        // New user discovered
        msg.sender.active = true;
        msg.sender.updateTime = std::time(nullptr);
        AddOrUpdateUser(msg.sender);

        MsgLog("[MsgMng] USER DISCOVERED (BR_ENTRY): " +
               msg.sender.userName + "@" + msg.sender.hostName +
               " (" + msg.sender.ipAddress + ":" + std::to_string(msg.sender.portNo) + ")" +
               ", nickname=" + msg.sender.nickName +
               ", group=" + msg.sender.groupName);

        if (onUserDiscovered_) {
            onUserDiscovered_(msg.sender);
        }

        // Reply with ANSENTRY
        {
            std::string body = localUser_.nickName;
            std::string extra = localUser_.groupName;
            auto reply = MakeMsg(MakePacketNo(),
                IPMSG_ANSENTRY | IPMSG_CAPUTF8OPT,
                body, extra);
            // Send to sender's UDP source port (original behavior)
            UdpSend(msg.sender.ipAddress, msg.sender.portNo, reply);
            // If the sender appears to be on the same host, also try their default port.
            // This enables cross-instance discovery on the same machine with different ports.
            if (msg.sender.ipAddress == "127.0.0.1" || msg.sender.ipAddress == "0.0.0.0") {
                // Try common ports: 2425 (default), 2525 (test server), and portNo_ (our port)
                for (int tryPort : {2425, 2525, portNo_}) {
                    if (tryPort != msg.sender.portNo && tryPort != portNo_) {
                        UdpSend(msg.sender.ipAddress, tryPort, reply);
                    }
                }
            }
        }
        break;
    }

    case IPMSG_ANSENTRY: {
        // Response to our BR_ENTRY
        msg.sender.active = true;
        msg.sender.updateTime = std::time(nullptr);
        AddOrUpdateUser(msg.sender);

        MsgLog("[MsgMng] USER DISCOVERED (ANSENTRY): " +
               msg.sender.userName + "@" + msg.sender.hostName +
               " (" + msg.sender.ipAddress + ":" + std::to_string(msg.sender.portNo) + ")" +
               ", nickname=" + msg.sender.nickName +
               ", group=" + msg.sender.groupName);

        if (onUserDiscovered_) {
            onUserDiscovered_(msg.sender);
        }
        break;
    }

    case IPMSG_BR_EXIT: {
        RemoveUser(msg.sender.Key());
        if (onUserLeft_) {
            onUserLeft_(msg.sender);
        }
        break;
    }

    case IPMSG_BR_ABSENCE: {
        msg.sender.hostStatus = GET_OPT(msg.command);
        msg.sender.updateTime = std::time(nullptr);
        AddOrUpdateUser(msg.sender);

        if (onUserStatusChanged_) {
            onUserStatusChanged_(msg.sender);
        }
        break;
    }

    case IPMSG_SENDMSG: {
        // Incoming message
        if (onMessageReceived_) {
            onMessageReceived_(msg);
        }

        // Auto-acknowledge if requested
        if (msg.command & IPMSG_SENDCHECKOPT) {
            // Fix: use known user port (listening port) instead of ephemeral UDP source port.
            // The msg.sender.portNo was set from the UDP recvfrom source port, which is a
            // temporary port. Reply RECVMSG to the sender's actual listening port so
            // the other instance can receive it on its UDP listening socket.
            UserInfo replyTarget = msg.sender;
            auto knownUser = FindUser(msg.sender.Key());
            if (knownUser && knownUser->portNo != replyTarget.portNo) {
                replyTarget.portNo = knownUser->portNo;
            }
            SendRecvMsg(replyTarget, msg.packetNo);
        }
        break;
    }

    case IPMSG_RECVMSG: {
        // Acknowledgment received - forward to callback
        if (onMessageReceived_) {
            onMessageReceived_(msg);
        }
        break;
    }

    default:
        break;
    }
}

// ---------- Protocol ----------

std::string MsgMng::MakeMsg(uint64_t packetNo, uint32_t command,
                             const std::string& msg, const std::string& extra) {
    // IPMsg protocol format: "1:packetNo:userName:hostName:command:body\0extra\0extInfo"
    // Compatible with original IPMsg v3 protocol
    //
    // Encoding strategy:
    //   - localUser_ fields are always stored as UTF-8 internally
    //   - Without IPMSG_UTF8OPT: convert to GBK for legacy clients (FeiQ etc.)
    //   - With IPMSG_UTF8OPT: send as UTF-8 directly

    uint32_t mode = GET_MODE(command);
    bool isBrCmd = (mode == IPMSG_BR_ENTRY || mode == IPMSG_BR_EXIT ||
                    mode == IPMSG_BR_ABSENCE);
    bool isAnsEntry = (mode == IPMSG_ANSENTRY);
    bool isUtf8 = (command & IPMSG_UTF8OPT) != 0;

    // localUser_ stores UTF-8 internally.
    // If UTF8OPT flag is set, send as-is (UTF-8).
    // Otherwise, convert to GBK for compatibility with legacy clients like FeiQ.
    std::string userName = isUtf8 ? localUser_.userName : UTF8ToGBK(localUser_.userName);
    std::string hostName = isUtf8 ? localUser_.hostName : UTF8ToGBK(localUser_.hostName);
    std::string nickName = isUtf8 ? localUser_.nickName : UTF8ToGBK(localUser_.nickName);
    std::string groupName = isUtf8 ? localUser_.groupName : UTF8ToGBK(localUser_.groupName);

    // For BR commands, body=nickname and extra=groupName.
    // These must also be encoding-converted to match the header fields.
    // For SENDMSG, body is message content (converted separately in SendMessage).
    std::string body = msg;
    std::string extraStr = extra;
    if (!isUtf8) {
        if (isBrCmd || isAnsEntry) {
            body = UTF8ToGBK(msg);
            if (!extra.empty()) {
                extraStr = UTF8ToGBK(extra);
            }
        }
    }

    // Build header: "ver:packetNo:userName:hostName:command:"
    std::string result;
    result.reserve(MAX_UDPBUF);

    result += std::to_string(IPMSG_VERSION) + ":";
    result += std::to_string(packetNo) + ":";
    result += userName + ":";
    result += hostName + ":";
    result += std::to_string(command) + ":";

    // Append body (message text or nickname for BR commands)
    result += body;

    // Append extra (separated by '\0')
    if (!extraStr.empty()) {
        result += std::string(1, '\0');
        result += extraStr;
    }

    // For BR_ENTRY/BR_EXIT/BR_NOTIFY, append extended info after another '\0'
    // Format: \nUN:username\nHN:hostname\nNN:nickname\nGN:group\nVS:version
    if (isBrCmd) {
        result += std::string(1, '\0');
        result += "\nUN:" + userName;
        result += "\nHN:" + hostName;
        result += "\nNN:" + nickName;
        result += "\nGN:" + groupName;
        result += "\nVS:0x0003";
    }
    else if (isAnsEntry) {
        result += std::string(1, '\0');
        result += "\nVS:0x0003";
    }

    // Debug: log the outgoing BR/ANSENTRY message format
    if (isBrCmd || isAnsEntry) {
        std::string cmdName;
        if (mode == IPMSG_BR_ENTRY) cmdName = "BR_ENTRY";
        else if (mode == IPMSG_BR_EXIT) cmdName = "BR_EXIT";
        else if (mode == IPMSG_BR_ABSENCE) cmdName = "BR_ABSENCE";
        else if (mode == IPMSG_ANSENTRY) cmdName = "ANSENTRY";

        MsgLog("[SEND-" + cmdName + "] ===== OUTGOING MESSAGE FORMAT =====");
        std::stringstream cmdHex;
        cmdHex << std::hex << "0x" << command;
        MsgLog("[SEND-" + cmdName + "] Command raw: " + cmdHex.str() + 
               " (mode=" + std::to_string(mode) + ", UTF8OPT=" + (command & IPMSG_UTF8OPT ? "YES" : "NO") + ")");
        MsgLog("[SEND-" + cmdName + "] localUser_ (UTF-8 internal):");
        MsgLog("[SEND-" + cmdName + "]   userName = \"" + localUser_.userName + "\"");
        MsgLog("[SEND-" + cmdName + "]   hostName = \"" + localUser_.hostName + "\"");
        MsgLog("[SEND-" + cmdName + "]   nickName = \"" + localUser_.nickName + "\"");
        MsgLog("[SEND-" + cmdName + "]   groupName = \"" + localUser_.groupName + "\"");
        MsgLog("[SEND-" + cmdName + "] After encoding conversion (isUtf8=" + std::to_string(isUtf8) + "):");
        MsgLog("[SEND-" + cmdName + "]   userName(encoded) = \"" + userName + "\" (len=" + std::to_string(userName.size()) + ")");
        MsgLog("[SEND-" + cmdName + "]   hostName(encoded) = \"" + hostName + "\" (len=" + std::to_string(hostName.size()) + ")");
        MsgLog("[SEND-" + cmdName + "]   nickName(encoded) = \"" + nickName + "\" (len=" + std::to_string(nickName.size()) + ")");
        MsgLog("[SEND-" + cmdName + "]   groupName(encoded) = \"" + groupName + "\" (len=" + std::to_string(groupName.size()) + ")");
        MsgLog("[SEND-" + cmdName + "]   body(encoded) = \"" + body + "\" (len=" + std::to_string(body.size()) + ")");
        MsgLog("[SEND-" + cmdName + "]   extra(encoded) = \"" + extraStr + "\" (len=" + std::to_string(extraStr.size()) + ")");

        // Print hex dump of the full result
        MsgLog("[SEND-" + cmdName + "] Full packet hex dump (" + std::to_string(result.size()) + " bytes):");
        std::stringstream sendHex;
        for (size_t i = 0; i < result.size(); ++i) {
            sendHex << std::hex << std::setfill('0') << std::setw(2) 
                    << (unsigned int)(unsigned char)result[i] << " ";
            if ((i + 1) % 32 == 0 || i == result.size() - 1) {
                sendHex << " | ";
                size_t start = (i / 32) * 32;
                for (size_t j = start; j <= i; ++j) {
                    unsigned char c = result[j];
                    sendHex << std::dec << (c >= 32 && c < 127 ? (char)c : '.');
                }
                MsgLog(sendHex.str());
                sendHex.str("");
                sendHex.clear();
            }
        }
        MsgLog("[SEND-" + cmdName + "] ===== END OUTGOING MESSAGE =====");
    }

    return result;
}

bool MsgMng::ResolveMsg(const char* buf, int size,
                         const std::string& fromIP, int fromPort, MsgBuf& out) {
    // IPMsg protocol format: "ver:packetNo:userName:hostName:command:body[\0extra[\0extInfo]]"
    if (size <= 0) return false;

    // Log raw message for debugging
    std::string rawMsg(buf, size);
    MsgLog("========== RAW MESSAGE START (" + std::to_string(size) + " bytes from " + fromIP + ":" + std::to_string(fromPort) + ") ==========");
    
    // Print raw hex dump
    std::stringstream hexDump;
    for (int i = 0; i < size; ++i) {
        hexDump << std::hex << std::setfill('0') << std::setw(2) 
                << (unsigned int)(unsigned char)buf[i] << " ";
        if ((i + 1) % 16 == 0 || i == size - 1) {
            hexDump << " | ";
            int start = (i / 16) * 16;
            for (int j = start; j <= i; ++j) {
                unsigned char c = buf[j];
                hexDump << (c >= 32 && c < 127 ? (char)c : '.');
            }
            MsgLog(hexDump.str());
            hexDump.str("");
        }
    }
    
    // Print protocol fields
    MsgLog("Raw string (first 200 chars): " + rawMsg.substr(0, 200));
    MsgLog("========== RAW MESSAGE END ==========");
    
    // Parse header by finding the first 5 colon-separated fields
    // Fields: version, packetNo, userName, hostName, command
    // Everything after the 5th colon is the body
    const char* p = buf;
    const char* end = buf + size;
    std::string fields[5];

    for (int i = 0; i < 5; ++i) {
        const char* colon = static_cast<const char*>(memchr(p, ':', end - p));
        if (!colon) return false;

        fields[i] = std::string(p, colon - p);
        p = colon + 1;
    }

    // p now points to the body
    const char* bodyPtr = p;
    int bodyLen = static_cast<int>(end - bodyPtr);

    try {
        // Handle FeiQ extended version format: "1_lbt6_0#128#30B49EAE34C4#0#0#0#4001#9"
        // Extract only the numeric part before the first non-digit character
        std::string verStr = fields[0];
        size_t verEnd = verStr.find_first_not_of("0123456789");
        if (verEnd != std::string::npos) {
            verStr = verStr.substr(0, verEnd);
            MsgLog("FeiQ extended version detected: " + fields[0] + ", using: " + verStr);
        }
        int version = std::stoi(verStr);
        if (version != IPMSG_VERSION) return false;

        out.packetNo = std::stoull(fields[1]);
        
        // Convert username and hostname to UTF-8 if needed
        out.sender.userName = EnsureUTF8(fields[2]);
        out.sender.hostName = EnsureUTF8(fields[3]);
        
        out.command = std::stoul(fields[4]);
        out.sender.ipAddress = fromIP;
        out.sender.portNo = fromPort;
        out.timestamp = std::time(nullptr);
        
        MsgLog("Parsed header: ver=" + fields[0] + " pkt=" + fields[1] + 
               " user=" + out.sender.userName + " host=" + out.sender.hostName + 
               " cmd=" + std::to_string(out.command));
    } catch (...) {
        MsgLog("Failed to parse header");
        return false;
    }

    uint32_t mode = GET_MODE(out.command);

    // Find null byte separating body from extra
    int bodyStrLen = static_cast<int>(strnlen(bodyPtr, bodyLen));
    out.body = std::string(bodyPtr, bodyStrLen);

    // Convert body to UTF-8 if sender is not using UTF-8 (e.g. FeiQ sends GBK)
    // Only convert SENDMSG bodies (not BR_* messages)
    if (mode == IPMSG_SENDMSG || mode == IPMSG_RECVMSG) {
        if (!(out.command & IPMSG_UTF8OPT)) {
            out.body = EnsureUTF8(out.body);
        }
    }

    // Find extra (after first null byte)
    const char* extraPtr = nullptr;
    int extraLen = 0;
    if (bodyStrLen + 1 < bodyLen) {
        extraPtr = bodyPtr + bodyStrLen + 1;
        int remaining = bodyLen - bodyStrLen - 1;
        extraLen = static_cast<int>(strnlen(extraPtr, remaining));
        out.extra = std::string(extraPtr, extraLen);
    }

    // Find extInfo (after second null byte) — for CAPUTF8OPT messages
    const char* extInfoPtr = nullptr;
    int extInfoLen = 0;
    if (extraPtr && (extraPtr + extraLen + 1) < end) {
        extInfoPtr = extraPtr + extraLen + 1;
        extInfoLen = static_cast<int>(end - extInfoPtr);
    }

    // Parse nickname and group for BR_ENTRY / BR_NOTIFY / ANSENTRY
    if (mode == IPMSG_BR_ENTRY || mode == IPMSG_BR_ABSENCE ||
        mode == IPMSG_ANSENTRY) {
        // Nickname is in the body - convert to UTF-8 if needed
        out.sender.nickName = EnsureUTF8(out.body);

        // Group is in the extra (simple format)
        if (!out.extra.empty() && out.extra[0] != '\n') {
            out.sender.groupName = EnsureUTF8(out.extra);
        }
    }

    // Parse extended info lines (\nUN:...\nHN:...\nNN:...\nGN:...\nVS:...)
    if (extInfoPtr && extInfoLen > 0 && (out.command & IPMSG_CAPUTF8OPT)) {
        std::string extInfo(extInfoPtr, extInfoLen);
        std::istringstream iss(extInfo);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;

            if (line.compare(0, 3, "UN:") == 0) {
                out.sender.userName = EnsureUTF8(line.substr(3));
            } else if (line.compare(0, 3, "HN:") == 0) {
                out.sender.hostName = EnsureUTF8(line.substr(3));
            } else if (line.compare(0, 3, "NN:") == 0) {
                out.sender.nickName = EnsureUTF8(line.substr(3));
            } else if (line.compare(0, 3, "GN:") == 0) {
                out.sender.groupName = EnsureUTF8(line.substr(3));
            }
            // VS: version info - ignored for now
        }
    }

    out.sender.hostStatus = GET_OPT(out.command);
    
    MsgLog("Resolved message: mode=" + std::to_string(mode) + 
           " nick=" + out.sender.nickName + " group=" + out.sender.groupName);

    // Detailed logging for BR commands to debug nickname encoding
    if (mode == IPMSG_BR_ENTRY || mode == IPMSG_BR_ABSENCE || mode == IPMSG_ANSENTRY) {
        std::string cmdName;
        if (mode == IPMSG_BR_ENTRY) cmdName = "BR_ENTRY";
        else if (mode == IPMSG_BR_ABSENCE) cmdName = "BR_ABSENCE";
        else cmdName = "ANSENTRY";

        std::stringstream cmdHexRecv;
        cmdHexRecv << std::hex << "0x" << out.command;

        MsgLog("[RECV-" + cmdName + "] ===== INCOMING MESSAGE PARSED =====");
        MsgLog("[RECV-" + cmdName + "] From: " + fromIP + ":" + std::to_string(fromPort));
        MsgLog("[RECV-" + cmdName + "] Command: " + cmdHexRecv.str() +
               " (UTF8OPT=" + (out.command & IPMSG_UTF8OPT ? "YES" : "NO") +
               ", CAPUTF8OPT=" + (out.command & IPMSG_CAPUTF8OPT ? "YES" : "NO") + ")");
        MsgLog("[RECV-" + cmdName + "] Header fields (raw, before EnsureUTF8):");
        MsgLog("[RECV-" + cmdName + "]   fields[2] (userName) = \"" + fields[2] + "\" (len=" + std::to_string(fields[2].size()) + ")");
        MsgLog("[RECV-" + cmdName + "]   fields[3] (hostName) = \"" + fields[3] + "\" (len=" + std::to_string(fields[3].size()) + ")");
        
        // Print body hex to see encoding
        MsgLog("[RECV-" + cmdName + "] Body (raw bytes, len=" + std::to_string(out.body.size()) + "):");
        MsgLog("[RECV-" + cmdName + "]   body string = \"" + out.body + "\"");
        std::stringstream bodyHex;
        for (size_t i = 0; i < out.body.size() && i < 64; ++i) {
            bodyHex << std::hex << std::setfill('0') << std::setw(2) 
                    << (unsigned int)(unsigned char)out.body[i] << " ";
        }
        MsgLog("[RECV-" + cmdName + "]   body hex = " + bodyHex.str());

        MsgLog("[RECV-" + cmdName + "] Extra (raw bytes, len=" + std::to_string(out.extra.size()) + "):");
        MsgLog("[RECV-" + cmdName + "]   extra string = \"" + out.extra + "\"");

        if (extInfoPtr && extInfoLen > 0) {
            std::string extInfoRaw(extInfoPtr, extInfoLen);
            MsgLog("[RECV-" + cmdName + "] ExtInfo (raw, len=" + std::to_string(extInfoLen) + "):");
            MsgLog("[RECV-" + cmdName + "]   extInfo string = \"" + extInfoRaw + "\"");
            std::stringstream extInfoHex;
            for (int i = 0; i < extInfoLen && i < 64; ++i) {
                extInfoHex << std::hex << std::setfill('0') << std::setw(2) 
                          << (unsigned int)(unsigned char)extInfoPtr[i] << " ";
            }
            MsgLog("[RECV-" + cmdName + "]   extInfo hex = " + extInfoHex.str());
        }

        MsgLog("[RECV-" + cmdName + "] Final resolved user info:");
        MsgLog("[RECV-" + cmdName + "]   userName  = \"" + out.sender.userName + "\"");
        MsgLog("[RECV-" + cmdName + "]   hostName  = \"" + out.sender.hostName + "\"");
        MsgLog("[RECV-" + cmdName + "]   nickName  = \"" + out.sender.nickName + "\"");
        MsgLog("[RECV-" + cmdName + "]   groupName = \"" + out.sender.groupName + "\"");
        MsgLog("[RECV-" + cmdName + "] ===== END INCOMING MESSAGE =====");
    }

    return true;
}

bool MsgMng::UdpSend(const std::string& ip, int port, const std::string& data) {
    if (udpSock_ == INVALID_SOCKET) return false;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    addr.sin_port = htons(static_cast<u_short>(port));

    int sent = sendto(udpSock_, data.data(), static_cast<int>(data.size()), 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    return sent != SOCKET_ERROR;
}

void MsgMng::UdpBroadcast(const std::string& data) {
    // Broadcast to all auto-detected broadcast addresses only
    auto broadcasts = GetAllBroadcastAddresses();

    for (const auto& bc : broadcasts) {
        bool sent = UdpSend(bc, portNo_, data);
        MsgLog("[MsgMng] Broadcast to " + bc + ":" + std::to_string(portNo_) +
               " (size=" + std::to_string(data.size()) + " bytes) " +
               (sent ? "OK" : "FAILED"));
    }
}

uint64_t MsgMng::MakePacketNo() {
    return ++packetNo_;
}

// ---------- Broadcast / Discovery ----------

void MsgMng::BroadcastEntry() {
    // Body: nickname, Extra: groupname
    std::string body = localUser_.nickName;
    std::string extra = localUser_.groupName;
    auto msg = MakeMsg(MakePacketNo(),
        IPMSG_BR_ENTRY | IPMSG_CAPUTF8OPT,
        body, extra);
    
    MsgLog("[MsgMng] Broadcasting BR_ENTRY...");
    UdpBroadcast(msg);
}

void MsgMng::SendDirectEntry(const std::string& ip, int port) {
    std::string body = localUser_.nickName;
    std::string extra = localUser_.groupName;
    auto msg = MakeMsg(MakePacketNo(),
        IPMSG_BR_ENTRY | IPMSG_CAPUTF8OPT,
        body, extra);
    UdpSend(ip, port, msg);
}

void MsgMng::BroadcastExit() {
    auto msg = MakeMsg(MakePacketNo(),
        IPMSG_BR_EXIT,
        localUser_.nickName);
    UdpBroadcast(msg);
}

void MsgMng::BroadcastAbsence(uint32_t command) {
    std::string body = localUser_.nickName;
    std::string extra = localUser_.groupName;
    auto msg = MakeMsg(MakePacketNo(), command, body, extra);
    UdpBroadcast(msg);
}

void MsgMng::AddSegment(const std::string& broadcastAddr) {
    auto it = std::find(segments_.begin(), segments_.end(), broadcastAddr);
    if (it == segments_.end()) {
        segments_.push_back(broadcastAddr);
    }
}

void MsgMng::RemoveSegment(const std::string& broadcastAddr) {
    segments_.erase(
        std::remove(segments_.begin(), segments_.end(), broadcastAddr),
        segments_.end());
}

std::vector<std::string> MsgMng::GetSegments() const {
    return segments_;
}

// ---------- IP Range Scanning ----------

static uint32_t IpToUint32(const std::string& ip) {
    uint32_t result = 0;
    int parts[4] = {};
    int count = 0;
    std::string s = ip;
    for (int i = 0; i < 4 && !s.empty(); ++i) {
        auto pos = s.find('.');
        std::string part = (pos == std::string::npos) ? s : s.substr(0, pos);
        parts[i] = std::stoi(part);
        if (pos != std::string::npos) s = s.substr(pos + 1);
        else s.clear();
        count++;
    }
    if (count != 4) return 0;
    result = ((uint32_t)parts[0] << 24) | ((uint32_t)parts[1] << 16) |
             ((uint32_t)parts[2] << 8) | (uint32_t)parts[3];
    return result;
}

static std::string Uint32ToIpStr(uint32_t val) {
    return std::to_string((val >> 24) & 0xFF) + "." +
           std::to_string((val >> 16) & 0xFF) + "." +
           std::to_string((val >> 8) & 0xFF) + "." +
           std::to_string(val & 0xFF);
}

int MsgMng::ScanIpRange(const std::string& startIp, const std::string& endIp) {
    uint32_t start = IpToUint32(startIp);
    uint32_t end = IpToUint32(endIp);

    if (start == 0 || end == 0 || start > end) {
        MsgLog("[MsgMng] Invalid IP range: " + startIp + " ~ " + endIp);
        return 0;
    }

    // Limit to 1024 IPs per range to avoid flooding
    if (end - start + 1 > 1024) {
        MsgLog("[MsgMng] IP range too large, limiting to 1024");
        end = start + 1023;
    }

    int count = 0;
    MsgLog("[MsgMng] Scanning IP range: " + startIp + " ~ " + endIp + " (" + std::to_string(end - start + 1) + " IPs)");

    for (uint32_t ip = start; ip <= end; ++ip) {
        std::string ipStr = Uint32ToIpStr(ip);
        SendDirectEntry(ipStr, portNo_);
        count++;

        // Small delay between sends to avoid flooding the network
        if (count % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    MsgLog("[MsgMng] IP range scan complete: " + std::to_string(count) + " IPs scanned");
    return count;
}

void MsgMng::ScanIpRanges(const std::vector<std::pair<std::string, std::string>>& ranges) {
    MsgLog("[MsgMng] Scanning " + std::to_string(ranges.size()) + " IP ranges...");
    int total = 0;
    for (const auto& [startIp, endIp] : ranges) {
        total += ScanIpRange(startIp, endIp);
    }
    MsgLog("[MsgMng] All ranges scanned: " + std::to_string(total) + " total IPs");
}

// ---------- Message Sending ----------

bool MsgMng::SendMessage(const UserInfo& target, const std::string& message,
                          uint32_t options) {
    // Feiq/FeiQ does not properly handle IPMSG_UTF8OPT for Chinese text,
    // so we send GBK-encoded content without the UTF8 flag.
    // The message body is UTF-8 from the frontend; convert it to GBK.
    std::string gbkMessage = UTF8ToGBK(message);
    uint32_t cmd = IPMSG_SENDMSG | options;
    auto msg = MakeMsg(MakePacketNo(), cmd, gbkMessage);
    bool ok = UdpSend(target.ipAddress, target.portNo, msg);
    
    std::stringstream sendCmdHex;
    sendCmdHex << std::hex << "0x" << cmd;
    MsgLog("[MsgMng] SendMessage to " + target.Key() + " (" + target.ipAddress + ":" + std::to_string(target.portNo) + ")" +
           " len=" + std::to_string(message.size()) + " cmd=" + sendCmdHex.str() +
           " ok=" + (ok ? "true" : "false"));
    
    return ok;
}

uint64_t MsgMng::SendMessageWithFile(const UserInfo& target, const std::string& message,
                                      const std::string& fileAttachInfo, uint32_t options) {
    // Don't use IPMSG_UTF8OPT for file messages - FeiQ doesn't support it
    // FeiQ uses GBK encoding for file messages
    uint32_t cmd = IPMSG_SENDMSG | IPMSG_FILEATTACHOPT | options;
    uint64_t pktNo = MakePacketNo();
    auto msg = MakeMsg(pktNo, cmd, message, fileAttachInfo);
    
    // Debug: print the raw message for analysis (to both console and debug log)
    {
        std::string dbgMsg;
        for (size_t i = 0; i < msg.size() && i < 300; i++) {
            if (msg[i] == '\0') dbgMsg += "\\0";
            else if (msg[i] == '\x07') dbgMsg += "\\a";
            else dbgMsg += msg[i];
        }
        std::stringstream fileCmdHex;
        fileCmdHex << std::hex << "0x" << cmd;
        MsgLog("[SEND-FILE] pktNo=" + std::to_string(pktNo) + ", cmd=" + fileCmdHex.str() +
               ", msgLen=" + std::to_string(msg.size()));
        MsgLog("[SEND-FILE] rawMsg=" + dbgMsg);
        // Also write to debug log for file-based analysis
        MsgLog("[SEND-FILE-RAW] pktNo=" + std::to_string(pktNo) + ", cmd=0x" + 
               ([](uint32_t v)->std::string{std::ostringstream o;o<<std::hex<<v;return o.str();})(cmd) +
               ", rawMsg=" + dbgMsg);
    }
    
    bool ok = UdpSend(target.ipAddress, target.portNo, msg);
    return ok ? pktNo : 0;
}

bool MsgMng::SendRecvMsg(const UserInfo& target, uint64_t pktNo) {
    auto msg = MakeMsg(MakePacketNo(), IPMSG_RECVMSG, std::to_string(pktNo));
    return UdpSend(target.ipAddress, target.portNo, msg);
}

// ---------- User Management ----------

std::vector<UserInfo> MsgMng::GetUsers() const {
    std::lock_guard<std::mutex> lock(usersMutex_);
    return users_;
}

std::optional<UserInfo> MsgMng::FindUser(const std::string& key) const {
    std::lock_guard<std::mutex> lock(usersMutex_);
    for (const auto& u : users_) {
        if (u.Key() == key) return u;
    }
    return std::nullopt;
}

void MsgMng::UpdateLocalInfo(const std::string& nickName, const std::string& groupName) {
    localUser_.nickName = nickName;
    localUser_.groupName = groupName;

    // Re-broadcast entry with new info
    if (ready_) {
        BroadcastEntry();
    }
}

void MsgMng::AddOrUpdateUser(const UserInfo& user) {
    std::lock_guard<std::mutex> lock(usersMutex_);
    for (auto& u : users_) {
        if (u.Key() == user.Key()) {
            u = user;
            return;
        }
    }
    users_.push_back(user);
}

void MsgMng::RemoveUser(const std::string& key) {
    std::lock_guard<std::mutex> lock(usersMutex_);
    users_.erase(
        std::remove_if(users_.begin(), users_.end(),
            [&key](const UserInfo& u) { return u.Key() == key; }),
        users_.end());
}

} // namespace ipmsg
