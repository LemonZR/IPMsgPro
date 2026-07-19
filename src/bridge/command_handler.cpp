// ============================================================================
// Bridge Command Handler Implementation
// ============================================================================

#include "command_handler.h"
#include "ipmsg/protocol.h"
#include <ctime>
#include <random>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <tauricpp/dialog.hpp>
#include <shlobj.h>

// ============================================================================
// Utility: Convert GBK to UTF-8 on Windows
// ============================================================================
static std::string GbkToUtf8(const std::string& gbk) {
    if (gbk.empty()) return gbk;
    // Check if already valid UTF-8 (quick heuristic: no bytes >= 0x80 means ASCII)
    bool isAscii = true;
    for (unsigned char c : gbk) {
        if (c >= 0x80) { isAscii = false; break; }
    }
    if (isAscii) return gbk;

    // GBK -> UTF-16
    int wlen = MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return gbk;
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), -1, &wstr[0], wlen);

    // UTF-16 -> UTF-8
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return gbk;
    std::string utf8(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], ulen, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();

    return utf8;
}

static bool IsValidUtf8(const std::string& str) {
    // Quick check: if all bytes are ASCII, it's valid UTF-8
    bool hasNonAscii = false;
    for (unsigned char c : str) {
        if (c >= 0x80) { hasNonAscii = true; break; }
    }
    if (!hasNonAscii) return true;

    // Full validation
    const unsigned char* p = reinterpret_cast<const unsigned char*>(str.data());
    const unsigned char* end = p + str.size();
    while (p < end) {
        if (*p < 0x80) { ++p; continue; }
        else if (*p < 0xC0) return false; // Unexpected continuation byte
        else if (*p < 0xE0) { if (end - p < 2 || (p[1] & 0xC0) != 0x80) return false; p += 2; }
        else if (*p < 0xF0) { if (end - p < 3 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false; p += 3; }
        else if (*p < 0xF8) { if (end - p < 4 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return false; p += 4; }
        else return false;
    }
    return true;
}

static std::string EnsureUtf8(const std::string& str) {
    if (IsValidUtf8(str)) return str;
    return GbkToUtf8(str);
}
#include <iostream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace ipmsg {

namespace {
    // Simple file logger for debugging GUI message flow
    void WriteDebugLog(const std::string& msg) {
        // Use USERPROFILE\.ipmsgpro for debug log (same as data directory)
        char userProfile[MAX_PATH] = {};
        if (GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH) <= 0) {
            SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, userProfile);
        }
        std::string dir = std::string(userProfile) + "\\.ipmsgpro";
        CreateDirectoryA(dir.c_str(), nullptr);
        std::string logPath = dir + "\\ipmsg_gui_debug.log";
        
        std::ofstream log(logPath, std::ios::app);
        if (log.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            char buf[32] = {};
            tm local = {};
            localtime_s(&local, &tt);
            strftime(buf, sizeof(buf), "%H:%M:%S", &local);
            log << "[" << buf << "] " << msg << std::endl;
        }
    }
}

CommandHandler& CommandHandler::Instance() {
    static CommandHandler instance;
    return instance;
}

void CommandHandler::Init(tauricpp::Bridge& bridge, MsgMng& msgMng,
                           MessageDB& msgDb, FileTransferManager& fileTransfer) {
    bridge_ = &bridge;
    msgMng_ = &msgMng;
    msgDb_ = &msgDb;
    fileTransfer_ = &fileTransfer;
}

void CommandHandler::SetNativeWindowHandle(void* hwnd) {
    hwnd_ = static_cast<void*>(hwnd);
    WriteDebugLog("[DIALOG] SetNativeWindowHandle called, hwnd=" + 
                  (hwnd ? std::to_string(reinterpret_cast<uintptr_t>(hwnd)) : "NULL"));
}

void CommandHandler::RegisterAllCommands() {
    if (!bridge_) return;

    // User management
    bridge_->RegisterCommand("user.discover",
        [this](const nlohmann::json& args) { return HandleUserDiscover(args); });
    bridge_->RegisterCommand("user.list",
        [this](const nlohmann::json& args) { return HandleUserList(args); });
    bridge_->RegisterCommand("user.status",
        [this](const nlohmann::json& args) { return HandleUserStatus(args); });
    bridge_->RegisterCommand("user.local",
        [this](const nlohmann::json& args) { return HandleUserLocal(args); });

    // Message
    bridge_->RegisterCommand("message.send",
        [this](const nlohmann::json& args) { return HandleMessageSend(args); });
    bridge_->RegisterCommand("message.send_image",
        [this](const nlohmann::json& args) { return HandleMessageSendImage(args); });

    // File
    bridge_->RegisterCommand("file.send",
        [this](const nlohmann::json& args) { return HandleFileSend(args); });
    bridge_->RegisterCommand("file.recv",
        [this](const nlohmann::json& args) { return HandleFileRecv(args); });
    bridge_->RegisterCommand("file.save_temp",
        [this](const nlohmann::json& args) { return HandleFileSaveTemp(args); });
    bridge_->RegisterCommand("file.accept",
        [this](const nlohmann::json& args) { return HandleFileAccept(args); });
    bridge_->RegisterCommand("file.reject",
        [this](const nlohmann::json& args) { return HandleFileReject(args); });
    bridge_->RegisterCommand("file.open_folder",
        [this](const nlohmann::json& args) { return HandleFileOpenFolder(args); });

    // History
    bridge_->RegisterCommand("history.get",
        [this](const nlohmann::json& args) { return HandleHistoryGet(args); });
    bridge_->RegisterCommand("history.search",
        [this](const nlohmann::json& args) { return HandleHistorySearch(args); });
    bridge_->RegisterCommand("history.clear",
        [this](const nlohmann::json& args) { return HandleHistoryClear(args); });

    // Network
    bridge_->RegisterCommand("network.scan",
        [this](const nlohmann::json& args) { return HandleNetworkScan(args); });

    // Config
    bridge_->RegisterCommand("config.set",
        [this](const nlohmann::json& args) { return HandleConfigSet(args); });

    // Dialog
    bridge_->RegisterCommand("dialog.pick_folder",
        [this](const nlohmann::json& args) { return HandleDialogPickFolder(args); });

}

void CommandHandler::SetupEventForwarding() {
    if (!bridge_ || !msgMng_) return;

    // Setup file transfer progress callback
    fileTransfer_->SetProgressCallback([this](const ipmsg::TransferProgress& progress) {
        nlohmann::json event = {
            {"transferId", progress.transferId},
            {"filename", progress.filename},
            {"fileSize", progress.fileSize},
            {"transferred", progress.transferred},
            {"status", static_cast<int>(progress.status)},
            {"isSending", progress.isSending}
        };

        WriteDebugLog("[PROGRESS-CB] transferId=" + progress.transferId +
                     ", status=" + std::to_string(static_cast<int>(progress.status)) +
                     ", transferred=" + std::to_string(progress.transferred) +
                     "/" + std::to_string(progress.fileSize) +
                     ", isSending=" + std::to_string(progress.isSending));

        if (progress.status == ipmsg::TransferStatus::Completed) {
            // File transfer completed
            event["message"] = progress.isSending ? "File sent successfully" : "File received successfully";
            if (!progress.isSending) {
                event["savePath"] = progress.localPath;
            }
            WriteDebugLog("[PROGRESS-CB] Emitting file.transfer_completed for transferId=" + progress.transferId);
            bridge_->Emit("file.transfer_completed", event);
        } else if (progress.status == ipmsg::TransferStatus::Failed) {
            // File transfer failed
            event["message"] = "File transfer failed";
            bridge_->Emit("file.transfer_failed", event);
        } else {
            // Progress update
            event["progress"] = progress.fileSize > 0 ?
                (progress.transferred * 100.0 / progress.fileSize) : 0.0;
            bridge_->Emit("file.transfer_progress", event);
        }
    });

    msgMng_->SetUserDiscoveredCallback([this](const ipmsg::UserInfo& user) {
        bridge_->Emit("user.discovered", UserToJson(user));
    });

    msgMng_->SetUserLeftCallback([this](const UserInfo& user) {
        bridge_->Emit("user.status_changed", {
            {"user", UserToJson(user)},
            {"status", "offline"}
        });
    });

    msgMng_->SetMessageReceivedCallback([this](const MsgBuf& msg) {
        try {
            // Log every received message for debugging
            {
                char cmdBuf[32] = {};
                snprintf(cmdBuf, sizeof(cmdBuf), "0x%08lx", (unsigned long)msg.command);
                uint32_t mode = GET_MODE(msg.command);
                std::string modeStr;
                switch (mode) {
                    case IPMSG_BR_ENTRY: modeStr = "BR_ENTRY"; break;
                    case IPMSG_BR_EXIT: modeStr = "BR_EXIT"; break;
                    case IPMSG_ANSENTRY: modeStr = "ANSENTRY"; break;
                    case IPMSG_SENDMSG: modeStr = "SENDMSG"; break;
                    case IPMSG_RECVMSG: modeStr = "RECVMSG"; break;
                    default: modeStr = "UNKNOWN"; break;
                }
                std::cout << "[GUI-MSG] ====== BEGIN MESSAGE ======" << std::endl;
                std::cout << "[GUI-MSG] packetNo=" << msg.packetNo << std::endl;
                std::cout << "[GUI-MSG] from=" << msg.sender.userName << "@"
                          << msg.sender.hostName << " (" << msg.sender.ipAddress << ":" 
                          << msg.sender.portNo << ")" << std::endl;
                std::cout << "[GUI-MSG] nickName=" << msg.sender.nickName 
                          << ", groupName=" << msg.sender.groupName << std::endl;
                std::cout << "[GUI-MSG] command=" << cmdBuf << " (" << modeStr << ")" << std::endl;
                std::cout << "[GUI-MSG] command_flags: ";
                if (msg.command & IPMSG_SENDCHECKOPT) std::cout << "SENDCHECKOPT ";
                if (msg.command & IPMSG_FILEATTACHOPT) std::cout << "FILEATTACHOPT ";
                if (msg.command & IPMSG_UTF8OPT) std::cout << "UTF8OPT ";
                if (msg.command & IPMSG_CAPUTF8OPT) std::cout << "CAPUTF8OPT ";
                std::cout << std::endl;
                std::cout << "[GUI-MSG] body=\"" << msg.body << "\" (len=" << msg.body.size() << ")" << std::endl;
                std::cout << "[GUI-MSG] extra=\"" << msg.extra << "\" (len=" << msg.extra.size() << ")" << std::endl;
                std::cout << "[GUI-MSG] extra_hex: ";
                for (size_t i = 0; i < msg.extra.size() && i < 200; ++i) {
                    std::cout << std::hex << std::setfill('0') << std::setw(2) 
                              << (unsigned int)(unsigned char)msg.extra[i] << " ";
                }
                std::cout << std::dec << std::endl;
                std::cout << "[GUI-MSG] ====== END MESSAGE ======" << std::endl;
                WriteDebugLog(std::string("[GUI-MSG] from=") + msg.sender.userName + "@" +
                              msg.sender.ipAddress + ":" + std::to_string(msg.sender.portNo) +
                              " cmd=" + cmdBuf + " mode=" + modeStr +
                              " body=\"" + msg.body + "\" extra=\"" + msg.extra + "\"");
            }

            // For RECVMSG (acknowledgment), forward to frontend as message status update
            uint32_t mode = GET_MODE(msg.command);
            if (mode == IPMSG_RECVMSG) {
                nlohmann::json ack = {
                    {"packetNo", msg.packetNo},
                    {"from", msg.sender.Key()}
                };
                bridge_->Emit("message.ack", ack);
                return;
            }

            // Only handle SENDMSG from here onwards
            if (mode != IPMSG_SENDMSG) return;

            // Ensure all sender fields are UTF-8 (FeiQ may send GBK even without UTF8OPT flag)
            auto& sender = const_cast<UserInfo&>(msg.sender);
            if (!(msg.command & IPMSG_UTF8OPT)) {
                // EnsureUtf8 is idempotent - safe to call even if already UTF-8
                sender.nickName = EnsureUtf8(sender.nickName);
                sender.groupName = EnsureUtf8(sender.groupName);
            }

            // Ensure extra is UTF-8 before constructing JSON
            std::string extraUtf8 = EnsureUtf8(msg.extra);

            // Determine message type based on command flags
            bool isFileAttach = (msg.command & IPMSG_FILEATTACHOPT) != 0;

            std::string msgType = "text";
            int dbType = 0;  // 0:text, 1:image, 2:file
            std::string fileName;
            int64_t fileSize = 0;
            int fileId = 0;

            if (isFileAttach && !extraUtf8.empty()) {
                // Parse file attachment info in IPMsg/Feiq format:
                // "fileId:filename:hexSize:hexMtime:hexFileType:\a"
                // Multiple files are separated by \a (0x07)
                // Colons in filenames are escaped as ::
                // Reference: Feiq feiqengine.cpp RecvFile::createFileContent

                // Parse fields from extra, handling :: escape and \a separator
                auto parseFileAttachInfo = [](const std::string& extra,
                                             int& outFileId, std::string& outFileName,
                                             int64_t& outFileSize) {
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
                                // Escaped colon ::
                                current += ':';
                                i++; // skip next colon
                            } else {
                                // Field separator
                                fields.push_back(current);
                                current.clear();
                            }
                        } else {
                            current += fileInfo[i];
                        }
                    }
                    fields.push_back(current); // last field

                    // Need at least 3 fields: fileId, filename, size
                    if (fields.size() < 3) return;

                    // IPMsg format: "fileNo:filename:hexSize:hexMtime:hexFileAttr[:extend-attr]"
                    // Field 0: fileNo (decimal) - used as fileId in GETFILEDATA
                    try { outFileId = std::stoi(fields[0]); } catch (...) {}

                    // Field 1: filename
                    outFileName = fields[1];

                    // Field 2: fileSize (hexadecimal)
                    try { outFileSize = std::stoll(fields[2], nullptr, 16); } catch (...) {}

                    // Field 3: mtime (hex, NOT used for GETFILEDATA)
                    // Field 4: fileAttr (hex, e.g. IPMSG_FILE_REGULAR=1)
                };

                parseFileAttachInfo(msg.extra, fileId, fileName, fileSize);

                // Log the raw extra bytes for comparison with our sending format
                {
                    std::ostringstream extraDbg;
                    extraDbg << "[RECV-FILE-EXTRA] Raw extra (" << msg.extra.size() << " bytes): ";
                    for (size_t i = 0; i < msg.extra.size() && i < 200; i++) {
                        unsigned char c = static_cast<unsigned char>(msg.extra[i]);
                        if (c == '\0') extraDbg << "\\0";
                        else if (c == '\x07') extraDbg << "\\a";
                        else if (c == '\n') extraDbg << "\\n";
                        else if (c >= 32 && c < 127) extraDbg << c;
                        else extraDbg << "<" << std::hex << (int)c << ">";
                    }
                    WriteDebugLog(extraDbg.str());
                }
                {
                    std::ostringstream detailOs;
                    detailOs << std::dec << "[RECV-FILE-EXTRA] Parsed: fileId=" << fileId 
                             << ", fileName=" << fileName << ", fileSize=" << fileSize;
                    WriteDebugLog(detailOs.str());
                }
                {
                    std::ostringstream detailOs;
                    detailOs << "[RECV-FILE-EXTRA] Original msg: packetNo=" << msg.packetNo 
                             << ", cmd=0x" << std::hex << msg.command << std::dec
                             << ", body='" << msg.body << "'";
                    WriteDebugLog(detailOs.str());
                }

                // Determine if image or file based on extension
                std::string ext = fileName;
                auto dotPos = ext.find_last_of('.');
                if (dotPos != std::string::npos) {
                    ext = ext.substr(dotPos + 1);
                } else {
                    ext.clear();
                }
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
                    ext == "bmp" || ext == "webp") {
                    msgType = "image";
                    dbType = 1;
                } else {
                    msgType = "file";
                    dbType = 2;
                }
            }

            nlohmann::json j = {
                {"id", std::to_string(msg.packetNo)},
                {"from", msg.sender.Key()},
                {"fromUser", UserToJson(msg.sender)},
                {"content", msg.body},
                {"type", msgType},
                {"timestamp", static_cast<int64_t>(msg.timestamp)},
                {"command", msg.command},
                {"extra", extraUtf8}
            };

            // Save to database (skip file attachments - they're managed by frontend via file.receive_request)
            // File attachment messages will be saved when the transfer completes
            if (!isFileAttach) {
                MessageRecord record;
                record.id = std::to_string(msg.packetNo);
                record.fromId = msg.sender.Key();
                record.toId = msgMng_->GetLocalUser().Key();
                record.content = msg.body;
                record.type = dbType;
                record.timestamp = static_cast<int64_t>(msg.timestamp);
                record.status = 1; // delivered
                msgDb_->SaveMessage(record);
            }

            // Emit message received event
            bridge_->Emit("message.received", j);

            // Do NOT auto-reply RECVMSG for file attachment notifications
            // RECVMSG should be sent when user clicks "Accept", not when notification is received
            // FeiQ interprets RECVMSG as "user accepted the file transfer"
            // If (isFileAttach) { ... }

            // If file attachment, emit file receive request event (NOT auto-accepting)
            if (isFileAttach && !fileName.empty()) {
                WriteDebugLog("[FILE_REQ_EMIT] packetNo=" + std::to_string(msg.packetNo) +
                              ", fromUser=" + msg.sender.Key() +
                              ", fromIp=" + msg.sender.ipAddress +
                              ", fromPort=" + std::to_string(msg.sender.portNo) +
                              ", fileName=" + fileName + 
                              ", fileSize=" + std::to_string(fileSize) +
                              ", fileId=" + std::to_string(fileId) +
                              ", transferId=" + std::to_string(msg.packetNo));
                
                WriteDebugLog("[BRIDGE_EMIT] Emitting file.receive_request event");
                bridge_->Emit("file.receive_request", {
                    {"packetNo", msg.packetNo},
                    {"fromUser", msg.sender.Key()},
                    {"fromUserIp", msg.sender.ipAddress},
                    {"fromUserPort", msg.sender.portNo},
                    {"fileName", fileName},
                    {"fileSize", fileSize},
                    {"fileId", fileId},
                    {"transferId", std::to_string(msg.packetNo)}
                });
            }
        } catch (const std::exception& e) {
            std::cerr << "[GUI-MSG] Exception in message callback: " << e.what() << std::endl;
            WriteDebugLog(std::string("[GUI-MSG] Exception: ") + e.what());
        } catch (...) {
            std::cerr << "[GUI-MSG] Unknown exception in message callback" << std::endl;
            WriteDebugLog("[GUI-MSG] Unknown exception");
        }
    });

    msgMng_->SetUserStatusChangedCallback([this](const UserInfo& user) {
        bool isAway = (user.hostStatus & IPMSG_ABSENCEOPT) != 0;
        bridge_->Emit("user.status_changed", {
            {"user", UserToJson(user)},
            {"status", isAway ? "away" : "online"}
        });
    });
}

// ---------- User Commands ----------

nlohmann::json CommandHandler::HandleUserDiscover(const nlohmann::json& args) {
    msgMng_->BroadcastEntry();
    return {{"success", true}};
}

nlohmann::json CommandHandler::HandleUserList(const nlohmann::json& args) {
    auto users = msgMng_->GetUsers();
    nlohmann::json userList = nlohmann::json::array();
    for (const auto& u : users) {
        userList.push_back(UserToJson(u));
    }
    return {{"users", userList}, {"count", users.size()}};
}

nlohmann::json CommandHandler::HandleUserStatus(const nlohmann::json& args) {
    std::string status = args.value("status", "online");
    uint32_t cmd = (status == "away") ?
        (IPMSG_BR_ABSENCE | IPMSG_ABSENCEOPT) :
        IPMSG_BR_ABSENCE;
    msgMng_->BroadcastAbsence(cmd);
    return {{"success", true}, {"status", status}};
}

nlohmann::json CommandHandler::HandleUserLocal(const nlohmann::json& args) {
    auto& localUser = msgMng_->GetLocalUser();
    return {
        {"success", true},
        {"id", localUser.Key()},
        {"nickname", localUser.nickName},
        {"username", localUser.userName},
        {"hostname", localUser.hostName},
        {"group", localUser.groupName},
        {"ip", localUser.ipAddress},
        {"port", localUser.portNo}
    };
}

nlohmann::json CommandHandler::HandleConfigSet(const nlohmann::json& args) {
    std::string nickname = args.value("nickname", "");
    std::string group = args.value("group", "");
    std::string dataDir = args.value("dataDir", "");
    
    if (!nickname.empty() || !group.empty()) {
        msgMng_->UpdateLocalInfo(nickname, group);
        std::cout << "[BACKEND-CONFIG] Updated: nickname=" << nickname << ", group=" << group << std::endl;
        WriteDebugLog("Config updated: nickname=" + nickname + ", group=" + group);
    }

    // Store custom data directory (used for downloads etc.)
    if (!dataDir.empty()) {
        dataDir_ = dataDir;
        CreateDirectoryA(dataDir_.c_str(), nullptr);
        WriteDebugLog("Config updated: dataDir=" + dataDir_);
    } else if (args.contains("dataDir") && args["dataDir"].is_string() && args["dataDir"].get<std::string>().empty()) {
        // dataDir explicitly set to empty -> reset to default
        dataDir_.clear();
        WriteDebugLog("Config updated: dataDir reset to default");
    }
    
    return {{"success", true}};
}

// ---------- Message Commands ----------

nlohmann::json CommandHandler::HandleMessageSend(const nlohmann::json& args) {
    auto target = FindUserFromArgs(args);
    if (!target) {
        return {{"success", false}, {"error", "Target user not found"}};
    }

    std::string content = args.value("content", "");
    if (content.empty()) {
        return {{"success", false}, {"error", "Message content is empty"}};
    }

    std::cout << "[BACKEND-SEND] TEXT to=" << target->Key() << ", content=\"" << content << "\"" << std::endl;

    // Normal mode: send via UDP
    // Try UTF-8 first (with IPMSG_UTF8OPT flag), fallback to GBK if needed.
    // Feiq/FeiQ handles IPMSG_UTF8OPT properly for UTF-8 encoded content.
    // For other IPMsg clients that don't understand UTF8OPT (like older FeiQ),
    // we send GBK encoded content without the UTF8 flag.
    bool ok = msgMng_->SendMessage(*target, content, IPMSG_SENDCHECKOPT);

    // Always save to database (even if send failed, we want to track it)
    MessageRecord record;
    {
        // Generate unique ID: timestamp_ms + random suffix to avoid collision
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);
        record.id = std::to_string(now) + "_" + std::to_string(dis(gen));
    }
    record.fromId = msgMng_->GetLocalUser().Key();
    record.toId = target->Key();
    record.content = content;
    record.type = 0;  // text
    record.timestamp = static_cast<int64_t>(std::time(nullptr));
    record.status = ok ? 0 : 2; // 0=sending, 2=failed
    msgDb_->SaveMessage(record);

    return {{"success", ok}, {"messageId", record.id}};
}

nlohmann::json CommandHandler::HandleMessageSendImage(const nlohmann::json& args) {
    auto target = FindUserFromArgs(args);
    if (!target) {
        return {{"success", false}, {"error", "Target user not found"}};
    }

    std::string filePath = args.value("filePath", "");
    if (filePath.empty()) {
        return {{"success", false}, {"error", "Image file path is empty"}};
    }

    std::cout << "[BACKEND-SEND] IMAGE to=" << target->Key() << ", filePath=\"" << filePath << "\"" << std::endl;

    // Start TCP file transfer (register file info for serving)
    std::string transferId = fileTransfer_->StartSendFile(
        target->ipAddress, target->portNo, filePath, target->Key());

    if (transferId.empty()) {
        return {{"success", false}, {"error", "Failed to start file transfer"}};
    }

    // Get file info
    auto fileInfo = fileTransfer_->GetFileInfo(transferId);
    if (!fileInfo) {
        return {{"success", false}, {"error", "Failed to get file info"}};
    }

    // Build file attach info for IPMsg protocol (Feiq format):
    // "fileId:filename:hexSize:hexMtime:hexFileType:\a"
    std::ostringstream attachOs;
    std::string escapedFileName = fileInfo->fileName;
    // Escape colons in filename (:: represents a literal colon)
    {
        std::string escaped;
        for (char c : escapedFileName) {
            if (c == ':') escaped += "::";
            else escaped += c;
        }
        escapedFileName = escaped;
    }
    attachOs << fileInfo->fileId << ":" << escapedFileName << ":"
             << std::hex << fileInfo->fileSize << ":"
             << fileInfo->modifyTime << ":"
             << fileInfo->fileAttr << ":\x07";
    std::string fileAttachInfo = attachOs.str();

    // Debug: print the file attach info with visible control chars
    {
        std::ostringstream dbgOs;
        for (size_t i = 0; i < fileAttachInfo.size(); i++) {
            unsigned char c = static_cast<unsigned char>(fileAttachInfo[i]);
            if (c == '\0') dbgOs << "\\0";
            else if (c == '\x07') dbgOs << "\\a";
            else if (c == '\n') dbgOs << "\\n";
            else if (c >= 32 && c < 127) dbgOs << c;
            else dbgOs << "<" << std::hex << (int)c << ">";
        }
        WriteDebugLog("[SEND-FILE-EXTRA] " + dbgOs.str());
        std::ostringstream detailOs;
        detailOs << std::dec << "[SEND-FILE-EXTRA] fileId=" << fileInfo->fileId 
                  << ", fileName=" << fileInfo->fileName
                  << ", fileSize=" << fileInfo->fileSize
                  << ", modifyTime=" << fileInfo->modifyTime
                  << ", fileAttr=" << fileInfo->fileAttr;
        WriteDebugLog(detailOs.str());
    }

    // Normal mode: send UDP notification with file attachment info
    uint64_t sentPktNo = msgMng_->SendMessageWithFile(*target, "[Image: " + fileInfo->fileName + "]", fileAttachInfo);

    if (sentPktNo > 0) {
        // Store the SENDMSG packetNo in FileInfo for matching GETFILEDATA requests
        {
            auto fi = fileTransfer_->GetFileInfo(transferId);
            if (fi) {
                fi->packetNo = sentPktNo;
                fileTransfer_->RegisterFileInfo(transferId, *fi);
            }
        }
        // Save to database
        MessageRecord record;
        record.id = transferId;
        record.fromId = msgMng_->GetLocalUser().Key();
        record.toId = target->Key();
        record.content = filePath;  // Store file path
        record.type = 1;  // image
        record.timestamp = static_cast<int64_t>(std::time(nullptr));
        record.status = 1; // sending
        msgDb_->SaveMessage(record);

        // Emit transfer started event
        bridge_->Emit("file.transfer_started", {
            {"transferId", transferId},
            {"filename", fileInfo->fileName},
            {"fileSize", fileInfo->fileSize},
            {"isSending", true},
            {"targetUser", target->Key()}
        });
    }

    return {{"success", true}, {"transferId", transferId}, {"fileName", fileInfo->fileName}};
}

// ---------- File Commands ----------

nlohmann::json CommandHandler::HandleFileSend(const nlohmann::json& args) {
    auto target = FindUserFromArgs(args);
    if (!target) {
        return {{"success", false}, {"error", "Target user not found"}};
    }

    std::string filePath = args.value("filePath", "");
    if (filePath.empty()) {
        return {{"success", false}, {"error", "File path is empty"}};
    }

    std::cout << "[BACKEND-SEND] FILE to=" << target->Key() << ", filePath=\"" << filePath << "\"" << std::endl;

    // Start TCP file transfer (register file info for serving)
    std::string transferId = fileTransfer_->StartSendFile(
        target->ipAddress, target->portNo, filePath, target->Key());

    if (transferId.empty()) {
        return {{"success", false}, {"error", "Failed to start file transfer"}};
    }

    // Get file info
    auto fileInfo = fileTransfer_->GetFileInfo(transferId);
    if (!fileInfo) {
        return {{"success", false}, {"error", "Failed to get file info"}};
    }

    // Build file attach info for IPMsg protocol (Feiq format):
    // "fileId:filename:hexSize:hexMtime:hexFileType:\a"
    std::ostringstream attachOs;
    std::string escapedFileName = fileInfo->fileName;
    // Escape colons in filename (:: represents a literal colon)
    {
        std::string escaped;
        for (char c : escapedFileName) {
            if (c == ':') escaped += "::";
            else escaped += c;
        }
        escapedFileName = escaped;
    }
    attachOs << fileInfo->fileId << ":" << escapedFileName << ":"
             << std::hex << fileInfo->fileSize << ":"
             << fileInfo->modifyTime << ":"
             << fileInfo->fileAttr << ":\x07";
    std::string fileAttachInfo = attachOs.str();

    // Debug: print the file attach info with visible control chars
    {
        std::ostringstream dbgOs;
        for (size_t i = 0; i < fileAttachInfo.size(); i++) {
            unsigned char c = static_cast<unsigned char>(fileAttachInfo[i]);
            if (c == '\0') dbgOs << "\\0";
            else if (c == '\x07') dbgOs << "\\a";
            else if (c == '\n') dbgOs << "\\n";
            else if (c >= 32 && c < 127) dbgOs << c;
            else dbgOs << "<" << std::hex << (int)c << ">";
        }
        WriteDebugLog("[SEND-FILE-EXTRA] " + dbgOs.str());
        std::ostringstream detailOs;
        detailOs << std::dec << "[SEND-FILE-EXTRA] fileId=" << fileInfo->fileId 
                  << ", fileName=" << fileInfo->fileName
                  << ", fileSize=" << fileInfo->fileSize
                  << ", modifyTime=" << fileInfo->modifyTime
                  << ", fileAttr=" << fileInfo->fileAttr;
        WriteDebugLog(detailOs.str());
    }

    // Normal mode: send UDP notification with file attachment info
    std::cout << "[BACKEND-SEND] Sending SENDMSG with FILEATTACHOPT to " << target->Key() 
              << " (fileId=" << fileInfo->fileId << ", fileSize=" << fileInfo->fileSize << ")" << std::endl;
    std::cout << "[BACKEND-SEND] File attach info: " << fileAttachInfo << std::endl;
    
    uint64_t sentPktNo = msgMng_->SendMessageWithFile(*target, "[File: " + fileInfo->fileName + "]", fileAttachInfo, IPMSG_SENDCHECKOPT);

    if (sentPktNo > 0) {
        std::cout << "[BACKEND-SEND] SENDMSG sent successfully, packetNo=" << sentPktNo << std::endl;
        // Store the SENDMSG packetNo in FileInfo for matching GETFILEDATA requests
        {
            auto fi = fileTransfer_->GetFileInfo(transferId);
            if (fi) {
                fi->packetNo = sentPktNo;
                fileTransfer_->RegisterFileInfo(transferId, *fi);
                std::cout << "[BACKEND-SEND] FileInfo updated: packetNo=" << sentPktNo << ", fileId=" << fi->fileId << std::endl;
            }
        }
        // Save to database
        MessageRecord record;
        record.id = transferId;
        record.fromId = msgMng_->GetLocalUser().Key();
        record.toId = target->Key();
        record.content = filePath;  // Store file path
        record.type = 2;  // file
        record.timestamp = static_cast<int64_t>(std::time(nullptr));
        record.status = 1; // sending
        msgDb_->SaveMessage(record);

        // Emit transfer started event
        bridge_->Emit("file.transfer_started", {
            {"transferId", transferId},
            {"filename", fileInfo->fileName},
            {"fileSize", fileInfo->fileSize},
            {"isSending", true},
            {"targetUser", target->Key()}
        });
    }

    return {{"success", true}, {"transferId", transferId}, {"fileName", fileInfo->fileName}};
}

nlohmann::json CommandHandler::HandleFileRecv(const nlohmann::json& args) {
    auto target = FindUserFromArgs(args);
    if (!target) {
        return {{"success", false}, {"error", "Target user not found"}};
    }

    std::string transferId = args.value("transferId", "");
    std::string fileName = args.value("fileName", "");
    int64_t fileSize = args.value("fileSize", 0);
    std::string savePath = args.value("savePath", "");
    uint64_t origPacketNo = args.value("packetNo", (uint64_t)0);
    int origFileId = args.value("fileId", 0);

    if (transferId.empty() || fileName.empty() || savePath.empty()) {
        return {{"success", false}, {"error", "Missing required parameters"}};
    }

    std::cout << "[BACKEND-RECV] FILE from=" << target->Key() << ", fileName=\"" << fileName << "\", fileSize=" << fileSize << ", savePath=\"" << savePath << "\"" << std::endl;

    // Start receiving file via TCP (same port as UDP per IPMsg protocol)
    std::string recvTransferId = fileTransfer_->StartRecvFile(
        target->ipAddress, target->portNo, fileName, fileSize, savePath, target->Key(),
        origPacketNo, origFileId);

    if (recvTransferId.empty()) {
        return {{"success", false}, {"error", "Failed to start file receive"}};
    }

    // Save to database
    MessageRecord record;
    record.id = recvTransferId;
    record.fromId = target->Key();
    record.toId = msgMng_->GetLocalUser().Key();
    record.content = savePath;  // Store save path
    record.type = 2;  // file
    record.timestamp = static_cast<int64_t>(std::time(nullptr));
    record.status = 1; // receiving
    msgDb_->SaveMessage(record);

    // Emit transfer started event
    bridge_->Emit("file.transfer_started", {
        {"transferId", recvTransferId},
        {"filename", fileName},
        {"fileSize", fileSize},
        {"isSending", false},
        {"targetUser", target->Key()},
        {"savePath", savePath}
    });

    return {{"success", true}, {"transferId", recvTransferId}};
}

nlohmann::json CommandHandler::HandleFileSaveTemp(const nlohmann::json& args) {
    std::string base64Data = args.value("data", "");
    std::string filename = args.value("filename", "temp_file");

    if (base64Data.empty()) {
        return {{"success", false}, {"error", "Base64 data is empty"}};
    }

    // Decode base64
    std::string decodedData;
    try {
        // Simple base64 decode implementation
        static const std::string base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

        int in_len = base64Data.size();
        int i = 0, j = 0;
        char char_array_4[4], char_array_3[3];

        while (in_len-- && base64Data[i] != '=' &&
               (isalnum(base64Data[i]) || base64Data[i] == '+' || base64Data[i] == '/')) {
            char_array_4[j++] = base64Data[i]; i++;
            if (j == 4) {
                for (j = 0; j < 4; j++)
                    char_array_4[j] = base64_chars.find(char_array_4[j]);
                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
                for (j = 0; j < 3; j++)
                    decodedData += char_array_3[j];
                j = 0;
            }
        }

        if (j) {
            for (int k = j; k < 4; k++)
                char_array_4[k] = 0;
            for (int k = 0; k < 4; k++)
                char_array_4[k] = base64_chars.find(char_array_4[k]);
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            for (int k = 0; k < j - 1; k++)
                decodedData += char_array_3[k];
        }
    } catch (...) {
        return {{"success", false}, {"error", "Failed to decode base64 data"}};
    }

    // Create temp directory
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string tempDir = std::string(tempPath) + "IPMsgPro";
    if (!CreateDirectoryA(tempDir.c_str(), nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            return {{"success", false}, {"error", "Failed to create temp dir: " + std::to_string(err)}};
        }
    }

    // Sanitize filename - remove path separators and special chars
    std::string safeFilename = filename;
    for (auto& c : safeFilename) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    if (safeFilename.empty()) safeFilename = "temp_file";

    // Generate unique filename
    std::string tempFile = tempDir + "\\" + std::to_string(std::time(nullptr)) + "_" + safeFilename;

    // Write to file
    std::ofstream outFile(tempFile, std::ios::binary);
    if (!outFile.is_open()) {
        DWORD err = GetLastError();
        return {{"success", false}, {"error", "Failed to create temp file: " + std::string(tempFile) + " err=" + std::to_string(err)}};
    }

    outFile.write(decodedData.data(), decodedData.size());
    outFile.close();

    return {{"success", true}, {"filePath", tempFile}};
}

nlohmann::json CommandHandler::HandleFileAccept(const nlohmann::json& args) {
    WriteDebugLog("[BACKEND-ACCEPT-ENTRY] file.accept called with args: " + args.dump());

    auto target = FindUserFromArgs(args);
    if (!target) {
        WriteDebugLog("[BACKEND-ACCEPT-ERROR] Target user not found! args=" + args.dump());
        return {{"success", false}, {"error", "Target user not found"}};
    }
    WriteDebugLog("[BACKEND-ACCEPT] Found target user: " + target->Key() + ", ip=" + target->ipAddress + ", port=" + std::to_string(target->portNo));

    std::string transferId = args.value("transferId", "");
    std::string fileName = args.value("fileName", "");
    int64_t fileSize = args.value("fileSize", 0);
    std::string savePath = args.value("savePath", "");
    uint64_t origPacketNo = args.value("packetNo", (uint64_t)0);
    int origFileId = args.value("fileId", 0);

    // If savePath is empty, auto-generate using data directory Downloads folder
    if (savePath.empty() && !fileName.empty()) {
        std::string baseDir = GetDataDir();
        std::string saveDir = baseDir + "\\Downloads";
        CreateDirectoryA(saveDir.c_str(), nullptr);
        savePath = saveDir + "\\" + fileName;
    }
    WriteDebugLog("[BACKEND-ACCEPT] savePath=" + savePath);

    if (transferId.empty() || fileName.empty() || savePath.empty()) {
        WriteDebugLog("[BACKEND-ACCEPT-ERROR] Missing required parameters!");
        return {{"success", false}, {"error", "Missing required parameters"}};
    }

    WriteDebugLog("[BACKEND-ACCEPT] Calling StartRecvFile: ip=" + target->ipAddress + ", port=" + std::to_string(target->portNo) + ", fileName=" + fileName + ", fileSize=" + std::to_string(fileSize) + ", savePath=" + savePath + ", origPacketNo=" + std::to_string(origPacketNo) + ", origFileId=" + std::to_string(origFileId));

    // Send IPMSG_RECVMSG acknowledgment to sender (delivery receipt)
    // Use the original SENDMSG packetNo (not the internal transferId)
    if (origPacketNo > 0) {
        msgMng_->SendRecvMsg(*target, origPacketNo);
    }

    // Normal mode: Start receiving file via TCP (same port as UDP per IPMsg protocol)
    // Pass origPacketNo and origFileId for building the GETFILEDATA request
    std::string recvTransferId = fileTransfer_->StartRecvFile(
        target->ipAddress, target->portNo, fileName, fileSize, savePath, target->Key(),
        origPacketNo, origFileId);

    WriteDebugLog("[BACKEND-ACCEPT] StartRecvFile result: recvTransferId=" + recvTransferId);

    if (recvTransferId.empty()) {
        WriteDebugLog("[BACKEND-ACCEPT-ERROR] StartRecvFile failed!");
        return {{"success", false}, {"error", "Failed to start file receive"}};
    }

    // Save to database
    MessageRecord record;
    record.id = recvTransferId;
    record.fromId = target->Key();
    record.toId = msgMng_->GetLocalUser().Key();
    record.content = savePath;
    record.type = 2;  // file
    record.timestamp = static_cast<int64_t>(std::time(nullptr));
    record.status = 1; // receiving
    msgDb_->SaveMessage(record);

    // Emit transfer started event
    bridge_->Emit("file.transfer_started", {
        {"transferId", recvTransferId},
        {"filename", fileName},
        {"fileSize", fileSize},
        {"isSending", false},
        {"targetUser", target->Key()},
        {"savePath", savePath}
    });

    return {{"success", true}, {"transferId", recvTransferId}};
}

nlohmann::json CommandHandler::HandleFileReject(const nlohmann::json& args) {
    auto target = FindUserFromArgs(args);
    if (!target) {
        return {{"success", false}, {"error", "Target user not found"}};
    }

    std::string transferId = args.value("transferId", "");

    // Send IPMSG_RELEASEFILES to sender to release the shared files
    // In IPMsg protocol, RELEASEFILES uses the same packetNo as the original SENDMSG
    uint64_t packetNo = 0;
    try { packetNo = std::stoull(transferId); } catch (...) {}

    if (packetNo > 0) {
        // Send RELEASEFILES command to notify sender
        std::string extra = std::to_string(packetNo);
        msgMng_->SendMessage(*target, extra,
            IPMSG_RELEASEFILES | IPMSG_FILEATTACHOPT);
    }

    return {{"success", true}};
}

nlohmann::json CommandHandler::HandleFileOpenFolder(const nlohmann::json& args) {
    std::string path = args.value("path", "");
    if (path.empty()) {
        return {{"success", false}, {"error", "Path is empty"}};
    }

    // Use ShellExecuteW to open explorer and select the file
    std::wstring wPath(path.begin(), path.end());
    std::wstring params = L"/select,\"" + wPath + L"\"";

    HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        L"explorer.exe",
        params.c_str(),
        nullptr,
        SW_SHOWNORMAL
    );

    // ShellExecuteW returns > 32 on success
    bool ok = reinterpret_cast<INT_PTR>(result) > 32;
    return {{"success", ok}};
}

// ---------- History Commands ----------

nlohmann::json CommandHandler::HandleHistoryGet(const nlohmann::json& args) {
    std::string userId = args.value("userId", "");
    int limit = args.value("limit", 50);
    int offset = args.value("offset", 0);

    // Get current user's ID
    std::string localUserId = msgMng_->GetLocalUser().Key();

    std::vector<MessageRecord> messages;
    bool ok = msgDb_->GetMessages(userId, localUserId, limit, offset, messages);

    nlohmann::json msgList = nlohmann::json::array();
    for (const auto& m : messages) {
        msgList.push_back({
            {"id", m.id},
            {"fromId", m.fromId},
            {"toId", m.toId},
            {"content", m.content},
            {"type", m.type},
            {"timestamp", m.timestamp},
            {"status", m.status}
        });
    }

    return {{"success", ok}, {"messages", msgList}, {"localUserId", localUserId}};
}

nlohmann::json CommandHandler::HandleHistorySearch(const nlohmann::json& args) {
    std::string keyword = args.value("keyword", "");
    if (keyword.empty()) {
        return {{"success", false}, {"error", "Keyword is empty"}};
    }

    std::vector<MessageRecord> messages;
    bool ok = msgDb_->SearchMessages(keyword, messages);

    nlohmann::json msgList = nlohmann::json::array();
    for (const auto& m : messages) {
        msgList.push_back({
            {"id", m.id},
            {"fromId", m.fromId},
            {"toId", m.toId},
            {"content", m.content},
            {"type", m.type},
            {"timestamp", m.timestamp},
            {"status", m.status}
        });
    }

    return {{"success", ok}, {"messages", msgList}};
}

nlohmann::json CommandHandler::HandleHistoryClear(const nlohmann::json& args) {
    std::string userId = args.value("userId", "");
    bool ok = msgDb_->ClearMessages(userId);
    return {{"success", ok}};
}

// ---------- Network Commands ----------

nlohmann::json CommandHandler::HandleNetworkScan(const nlohmann::json& args) {
    std::string segment = args.value("segment", "");
    if (!segment.empty()) {
        msgMng_->AddSegment(segment);
    }
    msgMng_->BroadcastEntry();
    return {{"success", true}};
}

// ---------- Helpers ----------

nlohmann::json CommandHandler::UserToJson(const UserInfo& user) {
    std::string status = "online";
    if (!user.active) status = "offline";
    else if (user.hostStatus & IPMSG_ABSENCEOPT) status = "away";

    return {
        {"id", user.Key()},
        {"nickname", user.nickName.empty() ? user.userName : user.nickName},
        {"username", user.userName},
        {"hostname", user.hostName},
        {"group", user.groupName},
        {"ip", user.ipAddress},
        {"port", user.portNo},
        {"status", status},
        {"version", ""}
    };
}

std::optional<UserInfo> CommandHandler::FindUserFromArgs(const nlohmann::json& args) {
    // Try finding by "target" (key or IP)
    if (args.contains("target")) {
        std::string target = args["target"].get<std::string>();

        // Try as key first
        auto user = msgMng_->FindUser(target);
        if (user) return user;

        // Try as IP address
        auto users = msgMng_->GetUsers();
        for (const auto& u : users) {
            if (u.ipAddress == target) return u;
        }
    }
    return std::nullopt;
}

// ---------- Dialog Commands ----------

nlohmann::json CommandHandler::HandleDialogPickFolder(const nlohmann::json& args) {
    std::string title = args.value("title", "Select Folder");
    WriteDebugLog("[DIALOG] HandleDialogPickFolder called, title=" + title);

    if (!hwnd_) {
        WriteDebugLog("[DIALOG] ERROR: hwnd_ is null!");
        return {{"success", false}, {"error", "Window handle not available"}};
    }

    WriteDebugLog("[DIALOG] hwnd_=" + std::to_string(reinterpret_cast<uintptr_t>(hwnd_)));
    HWND hWnd = static_cast<HWND>(hwnd_);
    WriteDebugLog("[DIALOG] Calling PickFolder with hWnd=" + std::to_string(reinterpret_cast<uintptr_t>(hWnd)));
    
    auto folder = tauricpp::Dialog::PickFolder(hWnd, title);
    WriteDebugLog("[DIALOG] PickFolder returned, folder=" + (folder ? *folder : "(empty)"));
    
    if (folder) {
        return {{"success", true}, {"folder", *folder}};
    }
    return {{"success", true}, {"folder", ""}};  // User cancelled
}

std::string CommandHandler::GetDataDir() const {
    // If custom dataDir is set, use it; otherwise use default (USERPROFILE\.ipmsgpro)
    if (!dataDir_.empty()) {
        return dataDir_;
    }
    char userProfile[MAX_PATH] = {};
    if (GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH) <= 0) {
        SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, userProfile);
    }
    return std::string(userProfile) + "\\.ipmsgpro";
}

} // namespace ipmsg
