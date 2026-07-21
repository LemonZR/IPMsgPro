#pragma once
// ============================================================================
// Bridge Command Handler
// Registers all Bridge commands for frontend-backend communication
// ============================================================================

#include <tauricpp/bridge.hpp>
#include <tauricpp/window.hpp>
#include "ipmsg/msgmng.h"
#include "database/message_db.h"
#include "file/file_transfer.h"
#include <memory>
#include <string>

namespace ipmsg {

class CommandHandler {
public:
    /// Get singleton instance
    static CommandHandler& Instance();

    /// Initialize with references to core components
    void Init(tauricpp::Bridge& bridge,
              MsgMng& msgMng,
              MessageDB& msgDb,
              FileTransferManager& fileTransfer);

    /// Set main window handle (call after window is created)
    void SetNativeWindowHandle(void* hwnd);

    /// Set window reference for window operations (show/hide/close)
    void SetWindow(tauricpp::Window* window);

    /// Get the minimize behavior setting
    std::string GetMinimizeBehavior() const { return minimizeBehavior_; }

    /// Get the notification sound setting
    bool GetNotificationSound() const { return notificationSound_; }

    /// Register all Bridge commands
    void RegisterAllCommands();

    /// Setup event forwarding (IPMsg events -> Bridge events)
    void SetupEventForwarding();

private:
    CommandHandler() = default;

    // --- User Commands ---
    nlohmann::json HandleUserDiscover(const nlohmann::json& args);
    nlohmann::json HandleUserList(const nlohmann::json& args);
    nlohmann::json HandleUserStatus(const nlohmann::json& args);
    nlohmann::json HandleUserLocal(const nlohmann::json& args);

    // --- Message Commands ---
    nlohmann::json HandleMessageSend(const nlohmann::json& args);
    nlohmann::json HandleMessageSendImage(const nlohmann::json& args);

    // --- File Commands ---
    nlohmann::json HandleFileSend(const nlohmann::json& args);
    nlohmann::json HandleFileInfo(const nlohmann::json& args);
    nlohmann::json HandleFileRecv(const nlohmann::json& args);
    nlohmann::json HandleFileSaveTemp(const nlohmann::json& args);
    nlohmann::json HandleFileAccept(const nlohmann::json& args);
    nlohmann::json HandleFileReject(const nlohmann::json& args);
    nlohmann::json HandleFileOpenFolder(const nlohmann::json& args);

    // --- History Commands ---
    nlohmann::json HandleHistoryGet(const nlohmann::json& args);
    nlohmann::json HandleHistorySearch(const nlohmann::json& args);
    nlohmann::json HandleHistoryClear(const nlohmann::json& args);

    // --- Network Commands ---
    nlohmann::json HandleNetworkScan(const nlohmann::json& args);

    // --- Config Commands ---
    nlohmann::json HandleConfigSet(const nlohmann::json& args);

    // --- Dialog Commands ---
    nlohmann::json HandleDialogPickFolder(const nlohmann::json& args);
    nlohmann::json HandleDialogOpen(const nlohmann::json& args);
    nlohmann::json HandleShellOpen(const nlohmann::json& args);

    // --- Helper: Convert UserInfo to JSON ---
    static nlohmann::json UserToJson(const UserInfo& user);

    // --- Helper: Find user by IP or key ---
    std::optional<UserInfo> FindUserFromArgs(const nlohmann::json& args);

    /// Get the effective data directory (custom or default)
    std::string GetDataDir() const;

private:
    tauricpp::Bridge* bridge_ = nullptr;
    MsgMng* msgMng_ = nullptr;
    MessageDB* msgDb_ = nullptr;
    FileTransferManager* fileTransfer_ = nullptr;
    tauricpp::Window* window_ = nullptr;  // Window reference for show/hide/close
    void* hwnd_ = nullptr;  // Main window handle for dialogs (cast to HWND in cpp)
    std::string dataDir_;   // Custom data directory (empty = use default)
    std::string minimizeBehavior_ = "taskbar";  // "taskbar" or "tray"
    bool notificationSound_ = true;  // play notification sound on new messages
};

} // namespace ipmsg
