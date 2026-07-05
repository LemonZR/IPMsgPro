// ============================================================================
// IPMsgPro Application Entry Point
// ============================================================================

// WinSock2 must come before Windows.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <ShlObj.h>

#include <tauricpp/app.hpp>
#include <tauricpp/dialog.hpp>

#include "bridge/command_handler.h"
#include "ipmsg/msgmng.h"
#include "database/message_db.h"
#include "file/file_transfer.h"

#include <string>
#include <atomic>
#include <cstring>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <nlohmann/json.hpp>

// ============================================================================
// Logger
// ============================================================================
static std::ofstream g_logFile;
static std::mutex g_logMutex;

static std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

static void Log(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::string logMsg = "[IPMSGPRO] [" + GetTimestamp() + "] [" + level + "] " + msg;
    std::cout << logMsg << std::endl;
    if (g_logFile.is_open()) {
        g_logFile << logMsg << std::endl;
        g_logFile.flush();
    }
}

#define LOG_INFO(msg) Log("INFO", msg)
#define LOG_WARN(msg) Log("WARN", msg)
#define LOG_ERROR(msg) Log("ERROR", msg)
#define LOG_DEBUG(msg) Log("DEBUG", msg)

// Global components (managed by main)
static ipmsg::MsgMng* g_msgMng = nullptr;
static ipmsg::MessageDB* g_msgDb = nullptr;
static ipmsg::FileTransferManager* g_fileTransfer = nullptr;

/// Get the application data directory for storing database etc.
/// Uses the directory where the executable is located
static std::string GetAppDataDir(int port) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir = std::string(exePath);
    dir = dir.substr(0, dir.find_last_of('\\'));
    if (port != ipmsg::IPMSG_DEFAULT_PORT) {
        dir += "\\IPMsgPro_" + std::to_string(port);
    } else {
        dir += "\\IPMsgPro";
    }
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

// ============================================================================
// Helper: Parse colon-separated file attachment fields (:: escaped colons)
// ============================================================================
static bool ParseFileAttachExtra(const std::string& extra, int& outFileId,
                                  std::string& outFileName, int64_t& outFileSize) {
    // Split by \a (0x07) for multiple files - take first file only
    std::string fileInfo = extra;
    auto sepPos = extra.find('\x07');
    if (sepPos != std::string::npos) {
        fileInfo = extra.substr(0, sepPos);
    }

    // Parse colon-separated fields, with :: escape
    std::vector<std::string> fields;
    std::string current;
    for (size_t i = 0; i < fileInfo.size(); i++) {
        if (fileInfo[i] == ':') {
            if (i + 1 < fileInfo.size() && fileInfo[i + 1] == ':') {
                current += ':';
                i++;
            } else {
                fields.push_back(current);
                current.clear();
            }
        } else {
            current += fileInfo[i];
        }
    }
    fields.push_back(current);

    if (fields.size() < 3) return false;

    try { outFileId = std::stoi(fields[0]); } catch (...) {}
    outFileName = fields[1];
    try { outFileSize = std::stoll(fields[2], nullptr, 16); } catch (...) {}
    return true;
}

// ============================================================================
// CLI Mode - Server (auto-accept all file transfers)
// ============================================================================
static void RunCliServer(int port) {
    LOG_INFO("CLI SERVER mode on port " + std::to_string(port));
    LOG_INFO("Waiting for file transfers... Auto-accept enabled.");

    // Set up auto-accept callback
    g_fileTransfer->SetFileReceiveRequestCallback(
        [](const std::string& fromUser, const std::string& fileName,
           int64_t fileSize, const std::string& transferId) {
            LOG_INFO("[AUTO-ACCEPT] File: " + fileName + " (" +
                     std::to_string(fileSize) + " bytes) from " + fromUser +
                     " transferId=" + transferId);
        }
    );

    // Set up progress callback
    g_fileTransfer->SetProgressCallback(
        [](const ipmsg::TransferProgress& progress) {
            if (progress.status == ipmsg::TransferStatus::Completed) {
                std::string dir = progress.isSending ? "Sent" : "Received";
                LOG_INFO("[TRANSFER " + dir + "] " + progress.filename +
                         " (" + std::to_string(progress.fileSize) + " bytes)" +
                         " path=" + progress.localPath);
            } else if (progress.status == ipmsg::TransferStatus::Failed) {
                LOG_ERROR("[TRANSFER FAILED] " + progress.filename);
            } else if (progress.status == ipmsg::TransferStatus::Transferring) {
                int pct = progress.fileSize > 0
                    ? (int)(progress.transferred * 100 / progress.fileSize)
                    : 0;
                LOG_DEBUG("[TRANSFER PROGRESS] " + progress.filename +
                          " " + std::to_string(pct) + "%");
            }
        }
    );

    // Watch for incoming messages with file attachments and auto-accept
    g_msgMng->SetMessageReceivedCallback(
        [](const ipmsg::MsgBuf& msg) {
            char cmdBuf[32] = {};
            snprintf(cmdBuf, sizeof(cmdBuf), "0x%08lx", (unsigned long)msg.command);
            LOG_INFO("[RECV] from " + msg.sender.userName + "@" +
                     msg.sender.ipAddress + " cmd=" + cmdBuf);

            uint32_t mode = msg.command & 0x000000ff;
            if (mode == 0x20 && (msg.command & 0x00200000)) {
                // SENDMSG | FILEATTACHOPT - auto-accept
                LOG_INFO("[FILE NOTIFICATION] from " + msg.sender.userName +
                         " packetNo=" + std::to_string(msg.packetNo));

                // Reply RECVMSG
                g_msgMng->SendRecvMsg(msg.sender, msg.packetNo);

                // Parse file info from extra
                int fileId = 0;
                std::string fileName;
                int64_t fileSize = 0;
                if (ParseFileAttachExtra(msg.extra, fileId, fileName, fileSize)) {
                    LOG_INFO("[AUTO-ACCEPT] File: " + fileName + " (" +
                             std::to_string(fileSize) + " bytes)");

                    // Generate save path
                    std::string saveDir = GetAppDataDir(g_msgMng->GetLocalUser().portNo) + "\\Downloads";
                    CreateDirectoryA(saveDir.c_str(), nullptr);
                    std::string savePath = saveDir + "\\" + fileName;

                    // Start receiving - use correct sender port
                    // The msg.sender.portNo may be a UDP ephemeral port; try to find the
                    // actual listening port from the user list.
                    int senderPort = msg.sender.portNo;
                    auto knownUser = g_msgMng->FindUser(msg.sender.Key());
                    if (knownUser && knownUser->portNo != msg.sender.portNo) {
                        LOG_INFO("[AUTO-ACCEPT] Using known port " + std::to_string(knownUser->portNo) +
                                 " instead of UDP source port " + std::to_string(msg.sender.portNo));
                        senderPort = knownUser->portNo;
                    }
                    std::string recvTransferId = g_fileTransfer->StartRecvFile(
                        msg.sender.ipAddress, senderPort,
                        fileName, fileSize, savePath,
                        msg.sender.Key(), msg.packetNo, fileId);

                    if (!recvTransferId.empty()) {
                        LOG_INFO("[AUTO-ACCEPT] Transfer started: " + recvTransferId);
                    } else {
                        LOG_ERROR("[AUTO-ACCEPT] Failed to start transfer");
                    }
                }
            } else if (mode == 0x20) {
                // Text message received
                LOG_INFO("[TEXT] from " + msg.sender.userName + "@" +
                         msg.sender.ipAddress + ":" + std::to_string(msg.sender.portNo) + ": " + msg.body);

                // Reply RECVMSG
                g_msgMng->SendRecvMsg(msg.sender, msg.packetNo);
                
                // Echo: send back the message with "Echo:" prefix
                // But don't echo messages that already start with "Echo:" (loop prevention)
                if (msg.body.substr(0, 5) != "Echo:") {
                    g_msgMng->SendMessage(msg.sender, "Echo:" + msg.body);
                    LOG_INFO("[ECHO] sent to " + msg.sender.userName + "@" +
                             msg.sender.ipAddress + ":" + std::to_string(msg.sender.portNo) + ": " + msg.body);
                }
            }
        }
    );

    // Watch for user discovery
    g_msgMng->SetUserDiscoveredCallback(
        [](const ipmsg::UserInfo& user) {
            LOG_INFO("[USER] Discovered: " + user.userName + "@" +
                     user.ipAddress + ":" + std::to_string(user.portNo));
        }
    );

    LOG_INFO("CLI Server ready. Listening on port " + std::to_string(port));

    // Keep running until Ctrl+C
    HANDLE hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetConsoleCtrlHandler([](DWORD) -> BOOL {
        LOG_INFO("Shutdown signal received");
        return FALSE;
    }, TRUE);

    // Periodically broadcast entry
    while (true) {
        g_msgMng->BroadcastEntry();
        WaitForSingleObject(hEvent, 30000); // broadcast every 30s
        // Check if handler set shutdown
        if (WaitForSingleObject(hEvent, 0) == WAIT_OBJECT_0) break;
    }
}

// ============================================================================
// CLI Mode - Test Runner (send text/image/file from JSON config)
// ============================================================================
struct TestConfig {
    int targetPort = 2425;
    std::string targetIp = "127.0.0.1";
    std::vector<nlohmann::json> testItems;
};

static TestConfig ParseTestConfig(const std::string& configPath) {
    TestConfig config;
    std::ifstream f(configPath);
    if (!f.is_open()) {
        LOG_ERROR("Cannot open config file: " + configPath);
        return config;
    }
    nlohmann::json j;
    f >> j;

    if (j.contains("target_ip")) config.targetIp = j["target_ip"];
    if (j.contains("target_port")) config.targetPort = j["target_port"];
    if (j.contains("tests") && j["tests"].is_array()) {
        config.testItems = j["tests"].get<std::vector<nlohmann::json>>();
    }
    return config;
}

static void RunCliTestRunner(int port, const std::string& configPath,
                              const std::string& targetIp, int targetPort) {
    LOG_INFO("CLI TEST RUNNER mode on port " + std::to_string(port));
    LOG_INFO("Target: " + targetIp + ":" + std::to_string(targetPort));

    // Load test config
    TestConfig config;
    if (!configPath.empty()) {
        config = ParseTestConfig(configPath);
        if (config.targetIp.empty()) config.targetIp = targetIp;
        if (config.targetPort == 0) config.targetPort = targetPort;
    } else {
        config.targetIp = targetIp;
        config.targetPort = targetPort;
    }

    LOG_INFO("Test target: " + config.targetIp + ":" + std::to_string(config.targetPort));

    // Set up progress callback for file transfers
    g_fileTransfer->SetProgressCallback(
        [](const ipmsg::TransferProgress& progress) {
            if (progress.status == ipmsg::TransferStatus::Completed) {
                std::string dir = progress.isSending ? "Sent" : "Received";
                if (progress.isSending) {
                    LOG_INFO("[SEND OK] " + progress.filename +
                             " (" + std::to_string(progress.fileSize) + " bytes)");
                }
            } else if (progress.status == ipmsg::TransferStatus::Failed) {
                LOG_ERROR("[SEND FAILED] " + progress.filename);
            } else if (progress.status == ipmsg::TransferStatus::Transferring && progress.isSending) {
                int pct = progress.fileSize > 0
                    ? (int)(progress.transferred * 100 / progress.fileSize)
                    : 0;
                LOG_DEBUG("[SEND PROGRESS] " + progress.filename + " " +
                          std::to_string(pct) + "%");
            }
        }
    );

    // Watch for incoming messages
    g_msgMng->SetMessageReceivedCallback(
        [](const ipmsg::MsgBuf& msg) {
            uint32_t mode = msg.command & 0x000000ff;
            if (mode == 0x20 && (msg.command & 0x00200000)) {
                // SENDMSG | FILEATTACHOPT - auto-accept for images
                LOG_INFO("[RECV FILE NOTIFY] from " + msg.sender.userName +
                         " packetNo=" + std::to_string(msg.packetNo));
                g_msgMng->SendRecvMsg(msg.sender, msg.packetNo);

                int fileId = 0;
                std::string fileName;
                int64_t fileSize = 0;
                if (ParseFileAttachExtra(msg.extra, fileId, fileName, fileSize)) {
                    LOG_INFO("[RECV FILE] " + fileName + " (" +
                             std::to_string(fileSize) + " bytes)");

                    std::string saveDir = GetAppDataDir(g_msgMng->GetLocalUser().portNo) + "\\Downloads";
                    CreateDirectoryA(saveDir.c_str(), nullptr);
                    std::string savePath = saveDir + "\\" + fileName;

                    std::string recvTransferId = g_fileTransfer->StartRecvFile(
                        msg.sender.ipAddress, msg.sender.portNo,
                        fileName, fileSize, savePath,
                        msg.sender.Key(), msg.packetNo, fileId);

                    if (!recvTransferId.empty()) {
                        LOG_INFO("[RECV STARTED] " + recvTransferId);
                    }
                }
            } else if (mode == 0x20 && !(msg.command & 0x00200000)) {
                // Text message received
                g_msgMng->SendRecvMsg(msg.sender, msg.packetNo);
                LOG_INFO("[RECV TEXT] from " + msg.sender.userName + "@" +
                         msg.sender.ipAddress + ": " + msg.body);
            } else if (mode == 0x21) {
                LOG_INFO("[RECVMSG ACK] from " + msg.sender.userName +
                         " pkt=" + msg.body);
            }
        }
    );

    // Watch for user discovery
    std::atomic<bool> targetDiscovered{false};
    g_msgMng->SetUserDiscoveredCallback(
        [&](const ipmsg::UserInfo& user) {
            LOG_INFO("[USER] Discovered: " + user.userName + "@" +
                     user.ipAddress + ":" + std::to_string(user.portNo));
            if (user.ipAddress == config.targetIp ||
                (config.targetIp == "127.0.0.1" && user.ipAddress != "0.0.0.0")) {
                targetDiscovered.store(true);
            }
        }
    );

    // Discover target user - broadcast BR_ENTRY
    LOG_INFO("Sending BR_ENTRY to discover target...");
    g_msgMng->BroadcastEntry();

    // Wait up to 5 seconds for target discovery
    for (int i = 0; i < 50; i++) {
        if (targetDiscovered.load()) {
            LOG_INFO("Target discovered!");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!targetDiscovered.load()) {
        LOG_WARN("Target not discovered via broadcast, will try direct send anyway");
    }

    // Find the target user
    auto target = g_msgMng->FindUser(config.targetIp);
    if (!target) {
        // Try by ip:port
        auto users = g_msgMng->GetUsers();
        for (const auto& u : users) {
            if (u.ipAddress == config.targetIp || u.portNo == config.targetPort) {
                target = u;
                break;
            }
        }
    }

    if (!target) {
        // Create virtual target for direct send
        ipmsg::UserInfo virtTarget;
        virtTarget.userName = "TestServer";
        virtTarget.hostName = "SERVER-PC";
        virtTarget.nickName = "TestServer";
        virtTarget.ipAddress = config.targetIp;
        virtTarget.portNo = config.targetPort;
        virtTarget.active = true;
        LOG_INFO("Using virtual target: " + virtTarget.userName + "@" +
                 virtTarget.ipAddress + ":" + std::to_string(virtTarget.portNo));
        target = virtTarget;
    } else {
        LOG_INFO("Found target: " + target->userName + "@" +
                 target->ipAddress + ":" + std::to_string(target->portNo));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // ============================================================
    // Execute tests
    // ============================================================
    if (!config.testItems.empty()) {
        LOG_INFO("=== Running " + std::to_string(config.testItems.size()) + " tests from config ===");
        for (size_t idx = 0; idx < config.testItems.size(); idx++) {
            const auto& item = config.testItems[idx];
            std::string type = item.value("type", "text");
            std::string content = item.value("content", "");

            LOG_INFO("--- Test " + std::to_string(idx + 1) + "/" +
                     std::to_string(config.testItems.size()) +
                     ": type=" + type + " ---");

            if (type == "text") {
                // Send text message
                bool ok = g_msgMng->SendMessage(*target, content);
                if (ok) {
                    LOG_INFO("[SEND TEXT] \"" + content + "\" -> OK");
                } else {
                    LOG_ERROR("[SEND TEXT] FAILED");
                }
            } else if (type == "file" || type == "image") {
                // Send file/image
                std::string filePath = content;
                if (filePath.empty()) {
                    LOG_ERROR("[SEND FILE] No file path specified");
                    continue;
                }

                // Check file exists
                std::ifstream checkFile(filePath, std::ios::binary);
                if (!checkFile.good()) {
                    LOG_ERROR("[SEND FILE] File not found: " + filePath);
                    continue;
                }
                checkFile.close();

                // Get file size
                std::ifstream fileSizeStream(filePath, std::ios::binary | std::ios::ate);
                int64_t fileSize = fileSizeStream.tellg();
                fileSizeStream.close();

                // Get file name
                auto lastSep = filePath.find_last_of("/\\");
                std::string fileName = (lastSep != std::string::npos)
                    ? filePath.substr(lastSep + 1) : filePath;

                // Register file for transfer
                std::string transferId = g_fileTransfer->StartSendFile(
                    target->ipAddress, target->portNo, filePath, target->Key());

                if (transferId.empty()) {
                    LOG_ERROR("[SEND FILE] Failed to register transfer");
                    continue;
                }

                // Get file info to get fileId
                auto fileInfo = g_fileTransfer->GetFileInfo(transferId);
                if (!fileInfo) {
                    LOG_ERROR("[SEND FILE] Failed to get file info");
                    continue;
                }

                // Build file attach info in Feiq format
                std::ostringstream attachOs;
                {
                    std::string escapedFileName = fileName;
                    // Escape colons
                    std::string escaped;
                    for (char c : escapedFileName) {
                        if (c == ':') escaped += "::";
                        else escaped += c;
                    }
                    escapedFileName = escaped;
                    attachOs << fileInfo->fileId << ":" << escapedFileName << ":"
                             << std::hex << fileSize << ":"
                             << 0 << ":"  // modify time
                             << 0 << ":\x07";  // file type
                }
                std::string fileAttachInfo = attachOs.str();

                // Send UDP notification
                uint64_t sentPktNo = g_msgMng->SendMessageWithFile(
                    *target, "[File: " + fileName + "]", fileAttachInfo);

                if (sentPktNo > 0) {
                    // Store packetNo for matching GETFILEDATA
                    auto fi = g_fileTransfer->GetFileInfo(transferId);
                    if (fi) {
                        fi->packetNo = sentPktNo;
                        g_fileTransfer->RegisterFileInfo(transferId, *fi);
                    }
                    LOG_INFO("[SEND FILE NOTIFY] " + fileName + " (" +
                             std::to_string(fileSize) + " bytes) pktNo=" +
                             std::to_string(sentPktNo) + " transferId=" + transferId);
                    LOG_INFO("[SEND FILE] Waiting for RECVMSG + TCP transfer...");
                } else {
                    LOG_ERROR("[SEND FILE NOTIFY] UDP send failed");
                }
            } else {
                LOG_WARN("[TEST] Unknown type: " + type);
            }

            // Wait between tests
            if (idx + 1 < config.testItems.size()) {
                int delay = item.value("delay_ms", 2000);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        }
    }

    // Keep running a bit to let file transfers complete
    LOG_INFO("Waiting for transfers to complete...");
    std::this_thread::sleep_for(std::chrono::seconds(10));

    LOG_INFO("=== CLI TEST RUNNER COMPLETE ===");
}

// ============================================================================
// Parse command line arguments
// ============================================================================
struct CliArgs {
    int port = ipmsg::IPMSG_DEFAULT_PORT;
    bool cliMode = false;
    std::string subCmd;        // "server" or "test"
    std::string configPath;    // path to JSON test config
    std::string targetIp = "127.0.0.1";
    int targetPort = 2425;

    // Users to auto-add in GUI mode (format: "ip:port" or "ip[:port][,ip2:port2]")
    std::vector<std::string> addUsers;
};

static CliArgs ParseCommandLine(LPSTR lpCmdLine) {
    CliArgs args;
    std::string cmdLine = lpCmdLine;

    // Parse --port
    size_t portPos = cmdLine.find("--port=");
    if (portPos != std::string::npos) {
        size_t start = portPos + 7;
        size_t end = cmdLine.find(' ', start);
        std::string portStr = (end != std::string::npos)
            ? cmdLine.substr(start, end - start)
            : cmdLine.substr(start);
        args.port = std::stoi(portStr);
    } else {
        portPos = cmdLine.find("--port ");
        if (portPos != std::string::npos) {
            size_t start = portPos + 7;
            while (start < cmdLine.size() && cmdLine[start] == ' ') ++start;
            size_t end = cmdLine.find(' ', start);
            std::string portStr = (end != std::string::npos)
                ? cmdLine.substr(start, end - start)
                : cmdLine.substr(start);
            args.port = std::stoi(portStr);
        }
    }

    // Parse --mode=cli
    if (cmdLine.find("--mode=cli") != std::string::npos) {
        args.cliMode = true;
    }

    // Parse --cmd=server or --cmd=test
    size_t cmdPos = cmdLine.find("--cmd=");
    if (cmdPos != std::string::npos) {
        size_t start = cmdPos + 6;
        size_t end = cmdLine.find(' ', start);
        args.subCmd = (end != std::string::npos)
            ? cmdLine.substr(start, end - start)
            : cmdLine.substr(start);
    }

    // Parse --config=<path>
    size_t cfgPos = cmdLine.find("--config=");
    if (cfgPos != std::string::npos) {
        size_t start = cfgPos + 9;
        size_t end = cmdLine.find(' ', start);
        args.configPath = (end != std::string::npos)
            ? cmdLine.substr(start, end - start)
            : cmdLine.substr(start);
    }

    // Parse --target=<ip>
    size_t tgtPos = cmdLine.find("--target=");
    if (tgtPos != std::string::npos) {
        size_t start = tgtPos + 9;
        size_t end = cmdLine.find(' ', start);
        std::string targetStr = (end != std::string::npos)
            ? cmdLine.substr(start, end - start)
            : cmdLine.substr(start);
        // Format: "ip:port" or just "ip"
        size_t colonPos = targetStr.find(':');
        if (colonPos != std::string::npos) {
            args.targetIp = targetStr.substr(0, colonPos);
            args.targetPort = std::stoi(targetStr.substr(colonPos + 1));
        } else {
            args.targetIp = targetStr;
        }
    }

    // Parse --adduser=<ip:port>[,<ip2:port2>,...]
    size_t addPos = cmdLine.find("--adduser=");
    if (addPos != std::string::npos) {
        size_t start = addPos + 10;
        size_t end = cmdLine.find(' ', start);
        std::string userStr = (end != std::string::npos)
            ? cmdLine.substr(start, end - start)
            : cmdLine.substr(start);
        // Split by comma
        size_t commaPos = 0;
        while (commaPos < userStr.size()) {
            size_t nextComma = userStr.find(',', commaPos);
            std::string entry = (nextComma != std::string::npos)
                ? userStr.substr(commaPos, nextComma - commaPos)
                : userStr.substr(commaPos);
            if (!entry.empty()) {
                args.addUsers.push_back(entry);
            }
            if (nextComma == std::string::npos) break;
            commaPos = nextComma + 1;
        }
    }

    return args;
}

// ============================================================================
// Main entry point
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    // Parse command line
    CliArgs cliArgs = ParseCommandLine(lpCmdLine);

    // In CLI mode, ensure stdout is unbuffered
    if (cliArgs.cliMode) {
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }

    // Initialize logger
    std::string dataDir = GetAppDataDir(cliArgs.port);
    std::string logPath = dataDir + "\\ipmsgpro.log";
    g_logFile.open(logPath, std::ios::app);
    if (!g_logFile.is_open()) {
        std::cerr << "Failed to open log file: " << logPath << std::endl;
    }
    
    LOG_INFO("========================================");
    LOG_INFO("IPMsgPro starting...");
    LOG_INFO("Log file: " + logPath);
    LOG_INFO("Port: " + std::to_string(cliArgs.port));
    if (cliArgs.cliMode) {
        LOG_INFO("Mode: CLI (" + cliArgs.subCmd + ")");
    }

    // Initialize Winsock
    if (!ipmsg::WSAInit()) {
        LOG_ERROR("Failed to initialize Winsock");
        return 1;
    }
    LOG_INFO("Winsock initialized");

    // Create core components
    g_msgMng = new ipmsg::MsgMng();
    g_msgDb = new ipmsg::MessageDB();
    g_fileTransfer = new ipmsg::FileTransferManager();
    LOG_INFO("Core components created");

    // Initialize message database (separate DB per port)
    std::string dbPath = dataDir + "\\ipmsg.db";
    if (!g_msgDb->Init(dbPath)) {
        LOG_ERROR("Failed to initialize database: " + dbPath);
    } else {
        LOG_INFO("Database initialized: " + dbPath);
    }

    // Initialize file transfer manager
    // Use MsgMng port for TCP file transfer (same port as Feiq protocol)
    // This ensures multiple instances on different UDP ports have different TCP ports
    g_fileTransfer->Init(cliArgs.port);
    LOG_INFO("File transfer manager initialized (TCP port=" +
             std::to_string(g_fileTransfer->GetTcpPort()) + ")");

    // ============================================================
    // CLI Mode (no GUI)
    // ============================================================
    if (cliArgs.cliMode) {
        // Initialize MsgMng with specified port
        if (!g_msgMng->Init(cliArgs.port)) {
            LOG_ERROR("Failed to initialize MsgMng on port " + std::to_string(cliArgs.port));
            delete g_fileTransfer;
            delete g_msgDb;
            delete g_msgMng;
            ipmsg::WSACleanup();
            return 1;
        }
        LOG_INFO("MsgMng initialized on port " + std::to_string(cliArgs.port));

        LOG_INFO("Local user: " + g_msgMng->GetLocalUser().userName +
                 "@" + g_msgMng->GetLocalUser().hostName + " (port=" +
                 std::to_string(cliArgs.port) + ")");

        if (cliArgs.subCmd == "server") {
            RunCliServer(cliArgs.port);
        } else if (cliArgs.subCmd == "test") {
            RunCliTestRunner(cliArgs.port, cliArgs.configPath,
                             cliArgs.targetIp, cliArgs.targetPort);
        } else {
            LOG_ERROR("Unknown CLI command: " + cliArgs.subCmd);
            LOG_INFO("Usage: IPMsgPro.exe --mode=cli --cmd=server|test --port=PORT [options]");
        }

        // Cleanup
        g_msgMng->Shutdown();
        delete g_fileTransfer;
        delete g_msgDb;
        delete g_msgMng;
        ipmsg::WSACleanup();
        if (g_logFile.is_open()) g_logFile.close();
        return 0;
    }

    // ============================================================
    // Normal GUI Mode
    // ============================================================

    // Configure application
    tauricpp::App::Config config;
    config.window_config.title = "倍信";
    config.window_config.width = 1200;
    config.window_config.height = 800;
    config.window_config.center = true;
    config.window_config.devtools = true;

    tauricpp::App app(config);
    LOG_INFO("TauriCPP app created");

    // Initialize command handler
    auto& cmdHandler = ipmsg::CommandHandler::Instance();
    cmdHandler.Init(app.GetBridge(), *g_msgMng, *g_msgDb, *g_fileTransfer);
    LOG_INFO("Command handler initialized");

    // Register all bridge commands
    cmdHandler.RegisterAllCommands();
    LOG_INFO("Bridge commands registered");

    // Setup app lifecycle
    static std::atomic<bool> g_running{true};

    app.OnSetup([&](tauricpp::App& app) {
        LOG_INFO("App setup callback started");
        
        // Set window close handler
        app.GetWindow().OnClose([&]() -> bool {
            LOG_INFO("Window close requested");
            g_running = false;
            return true;
        });

        // Initialize MsgMng (network layer) with specified port
        if (!g_msgMng->Init(cliArgs.port)) {
            LOG_ERROR("Failed to initialize MsgMng on port " + std::to_string(cliArgs.port));
        } else {
            LOG_INFO("MsgMng initialized on port " + std::to_string(cliArgs.port));
            LOG_INFO("Local user: " + g_msgMng->GetLocalUser().userName +
                     "@" + g_msgMng->GetLocalUser().hostName);
        }

        // Setup event forwarding after MsgMng is initialized
        cmdHandler.SetupEventForwarding();
        LOG_INFO("Event forwarding setup complete");

        // Broadcast entry to discover users on the network
        // Skip broadcast if --adduser was specified (we already know the target)
        if (cliArgs.addUsers.empty()) {
            g_msgMng->BroadcastEntry();
            LOG_INFO("Broadcast entry sent");
        } else {
            LOG_INFO("Skipping broadcast (--adduser specified, using direct discovery)");
        }

        // Auto-add users specified via --adduser argument
        for (const auto& userEntry : cliArgs.addUsers) {
            size_t colonPos = userEntry.find(':');
            std::string ip = userEntry;
            int port = ipmsg::IPMSG_DEFAULT_PORT;
            if (colonPos != std::string::npos) {
                ip = userEntry.substr(0, colonPos);
                port = std::stoi(userEntry.substr(colonPos + 1));
            }
            LOG_INFO("Auto-adding user: " + ip + ":" + std::to_string(port));
            // Send BR_ENTRY directly to the target's listening port so both sides
            // can discover each other (different ports can't discover via broadcast alone)
            g_msgMng->SendDirectEntry(ip, port);
        }
    });

    LOG_INFO("Starting app event loop...");
    
    // Run the application
    int result = app.Run();

    LOG_INFO("App event loop ended, cleaning up...");

    // Cleanup
    g_msgMng->Shutdown();
    ipmsg::WSACleanup();

    delete g_fileTransfer;
    delete g_msgDb;
    delete g_msgMng;

    LOG_INFO("Cleanup complete, exiting.");
    
    if (g_logFile.is_open()) {
        g_logFile.close();
    }

    return result;
}
