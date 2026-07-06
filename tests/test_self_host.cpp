// ============================================================================
// IPMsgPro Self-Test Host
// Creates two MsgMng instances on different ports and performs automated
// inter-communication testing: text message, file transfer, image transfer.
// All protocol and encoding validation is logged to stdout.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "iphlpapi")

#include "ipmsg/msgmng.h"
#include "ipmsg/protocol.h"
#include "file/file_transfer.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <map>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

// ============================================================================
// Logger
// ============================================================================
static std::mutex g_logMutex;

static std::string Timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

enum LogLevel { PASS, FAIL, INFO, WARN, DATA, PROTO };
static void Log(LogLevel level, const std::string& tag, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    const char* lvlStr = "";
    switch (level) {
        case PASS: lvlStr = "PASS"; break;
        case FAIL: lvlStr = "FAIL"; break;
        case INFO: lvlStr = "INFO"; break;
        case WARN: lvlStr = "WARN"; break;
        case DATA: lvlStr = "DATA"; break;
        case PROTO: lvlStr = "PROTO"; break;
    }
    std::cout << "[" << Timestamp() << "] [" << lvlStr << "] [" << tag << "] " << msg << std::endl;
}

// ============================================================================
// Test Configuration
// ============================================================================
constexpr int SERVER_PORT = 2525;
constexpr int CLIENT_PORT = 2425;
const std::string SERVER_NAME = "TestServer";
const std::string CLIENT_NAME = "TestClient";
const std::string LOCALHOST = "127.0.0.1";

// ============================================================================
// Test Result Tracking
// ============================================================================
struct TestCase {
    std::string name;
    bool passed = false;
    std::string detail;
};
static std::vector<TestCase> g_results;
static void TestResult(const std::string& name, bool passed, const std::string& detail = "") {
    g_results.push_back({name, passed, detail});
    Log(passed ? PASS : FAIL, "TEST", name + ": " + (passed ? "OK" : "FAILED") + (detail.empty() ? "" : " - " + detail));
}

// ============================================================================
// Globals
// ============================================================================
static std::atomic<bool> g_serverReceivedRecvMsg{false};
static std::atomic<bool> g_clientDiscoveredServer{false};
static std::atomic<int> g_serverMsgCount{0};
static std::atomic<int> g_clientMsgCount{0};
static std::atomic<uint64_t> g_lastPacketNo{0};
static std::string g_serverExtra; // store extra from received file notification
static std::mutex g_extraMutex;

// ============================================================================
// Create Test Files
// ============================================================================
static std::string CreateTestTextFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    f.write(content.data(), content.size());
    f.close();
    Log(INFO, "FILE", "Created: " + path + " (" + std::to_string(content.size()) + " bytes)");
    return path;
}

static std::string CreateTestImageFile(const std::string& path, int width, int height) {
    // Create a minimal valid BMP file (24-bit, no compression)
    std::ofstream f(path, std::ios::binary);
    
    // BMP header
    uint32_t pixelSize = width * height * 3;
    uint32_t fileSize = 54 + pixelSize;
    
    // BITMAPFILEHEADER
    uint16_t bfType = 0x4D42; // 'BM'
    f.write(reinterpret_cast<const char*>(&bfType), 2);
    uint32_t bfSize = fileSize;
    f.write(reinterpret_cast<const char*>(&bfSize), 4);
    uint16_t reserved1 = 0; f.write(reinterpret_cast<const char*>(&reserved1), 2);
    uint16_t reserved2 = 0; f.write(reinterpret_cast<const char*>(&reserved2), 2);
    uint32_t bfOffBits = 54;
    f.write(reinterpret_cast<const char*>(&bfOffBits), 4);
    
    // BITMAPINFOHEADER
    uint32_t biSize = 40;
    f.write(reinterpret_cast<const char*>(&biSize), 4);
    int32_t biWidth = width;
    f.write(reinterpret_cast<const char*>(&biWidth), 4);
    int32_t biHeight = height;
    f.write(reinterpret_cast<const char*>(&biHeight), 4);
    uint16_t biPlanes = 1; f.write(reinterpret_cast<const char*>(&biPlanes), 2);
    uint16_t biBitCount = 24; f.write(reinterpret_cast<const char*>(&biBitCount), 2);
    uint32_t biCompression = 0; f.write(reinterpret_cast<const char*>(&biCompression), 4);
    uint32_t biSizeImage = pixelSize;
    f.write(reinterpret_cast<const char*>(&biSizeImage), 4);
    int32_t biXPelsPerMeter = 2835; f.write(reinterpret_cast<