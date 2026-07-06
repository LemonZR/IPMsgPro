# IPMsg Pro v1.0.0

> A modern LAN instant messenger — compatible with FeiQ & IPMsg, better UI, better experience

A LAN instant messaging application built with [TauriCPP](https://github.com/masonwu21/TauriCPP) framework and [IPMsg v3.0 protocol](https://ipmsg.org/). Fully interoperable with FeiQ (飞秋) and IPMsg (飞鸽传书), while delivering a significantly more modern user experience.

## Why IPMsg Pro?

Still using FeiQ's 2008-era interface? Still忍受 IPMsg's bare-bones chat window? IPMsg Pro gives you the same LAN communication capability, but with a completely different experience:

| Feature | FeiQ | IPMsg | **IPMsg Pro** |
|---|:---|:---|:---|
| UI Style | Delphi classic style | Win32 native bare UI | **WeChat-style 3-panel modern UI** |
| Message Delivery Receipt | Only for file transfer | Only for file transfer | **Text message delivery receipts** |
| Image Sending | Not supported | Not supported | **Image preview + thumbnail display** |
| File Transfer Progress | Simple progress bar | No progress display | **Real-time progress + open folder** |
| Message History | No persistence | No persistence | **SQLite persistent storage + search** |
| User Nickname | Login name as nickname | Login name as nickname | **Custom nickname + groups** |
| Multi-subnet Discovery | Manual friend addition | Manual friend addition | **IP range auto-scan discovery** |
| Encoding Compatibility | GBK only | GBK/Shift-JIS only | **GBK/UTF-8 auto-conversion** |
| System Dependencies | Delphi runtime | None | **Single exe, no install dependency** |
| DPI Scaling | Not supported | Not supported | **High DPI adaptive** |
| Protocol Interop | FeiQ protocol | IPMsg v3 protocol | **Compatible with both** |
| Transfer Protocol | UDP+TCP | UDP+TCP | **UDP+TCP, fully compatible** |
| Open Source | Not open source | Open source (GPL) | **Open source (personal MIT)** |

**Key advantage**: Directly interoperable with FeiQ on the same network — no need for others to install IPMsg Pro. Messages sent from FeiQ are received by you, and your messages are received by FeiQ. But you enjoy a modern chat experience.

## Features

- **Auto User Discovery** — UDP broadcast auto-discovery of LAN users, supports multi-subnet IP range scanning
- **Text Messaging** — Real-time text message send/receive with delivery receipt confirmation
- **Image Sending** — Preview confirmation before sending, thumbnail display on receipt
- **File Transfer** — Send/receive confirmation flow, real-time progress display, one-click open folder
- **WeChat-style UI** — 3-panel layout: left conversation list, center user list, right chat panel
- **Data Persistence** — SQLite message history storage, local config persistence
- **Multi-subnet Discovery** — Configure IP ranges for auto-scanning, discover users across subnets
- **Unified Logging** — Debug build auto-enables logging, Release build runs silently

## Technical Highlights

### Architecture

- **Frontend/Backend Separation** — C++17 backend handles network communication, React frontend renders UI, communicating via Bridge
- **Single exe Deployment** — Frontend resources embedded in exe, zero install dependency, copy-and-run
- **WebView2 Rendering** — Uses system WebView2 runtime, no need to install a separate browser engine
- **Static CRT Linking** — Release build uses `/MT` static linking, no MSVC runtime DLL dependency

### Protocol Compatibility

- **Bidirectional Interop** — Fully interoperable with FeiQ and IPMsg v3, no need for others to switch
- **GBK/UTF-8 Dual Mode** — Auto-detects target client encoding preference, intelligently selects UTF-8 or GBK
- **CAPUTF8OPT Extension** — Supports IPMsg extension protocol, carrying full user info (nickname, group)
- **FeiQ Special Compatibility** — Adapted for multiple FeiQ protocol differences:
  - FeiQ doesn't support UTF8OPT for Chinese text → auto-downgrade to GBK encoding
  - FeiQ GETFILEDATA request lacks `\0` separator → compatible parsing
  - FeiQ extended version format → correctly identified

### Security & Performance

- **No External Services** — Pure LAN communication, data never passes through any cloud server
- **UDP Messages + TCP Files** — Messages delivered instantly via UDP, files transferred reliably via TCP
- **Efficient Encoding Conversion** — Uses Windows API for GBK↔UTF-8 conversion, zero third-party dependency

## Tech Stack

| Layer | Technology |
|---|---|
| Frontend | React + TypeScript + Tailwind CSS + Vite |
| Backend | C++17 + Win32 + WebView2 |
| Framework | TauriCPP (lightweight Tauri alternative, single exe, no install) |
| Protocol | IPMsg v3.0 (UDP 2425) + TCP file transfer |
| Database | SQLite3 |
| Communication | Bridge (frontend-backend JSON-RPC) |

## Project Structure

```
IPMsgPro/
├── src/                    # C++ backend
│   ├── main.cpp           # App entry, WebView2 window management
│   ├── ipmsg/             # IPMsg protocol implementation (ported from ipmsg-master)
│   │   ├── msgmng.cpp     # Core protocol: encode/decode, send/receive, encoding conversion
│   │   ├── network.cpp    # UDP/TCP network layer
│   │   └── protocol.h     # Protocol constant definitions
│   ├── bridge/            # Frontend-backend bridge command handling
│   ├── database/          # SQLite message storage
│   └── file/              # TCP file transfer management
├── frontend/              # React frontend
│   └── src/
│       ├── components/    # UI components (ChatPanel, UserList, Settings...)
│       ├── stores/        # Zustand state management
│       ├── services/      # Backend bridge service + IndexedDB config
│       └── types/         # TypeScript type definitions
├── TauriCPP/              # TauriCPP framework (submodule)
├── resources/             # App icons, resources
├── doc/                   # Documentation & promotional materials
└── CMakeLists.txt         # CMake build configuration
```

## Building

### Prerequisites

- Visual Studio 2022 (with C++ Desktop Development workload)
- CMake 3.15+
- Python 3 (for resource packaging)
- Node.js 18+
- vcpkg (for WebView2 SDK)

### Build Steps

```powershell
# 1. Install frontend dependencies
cd frontend
npm install

# 2. Build frontend
npm run build

# 3. Build C++ backend
cd ..
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Output: build/Release/IPMsgPro.exe
```

Or use the one-click build script:

```powershell
.\build.ps1
```

## Protocol Compatibility Details

Compatible with [IPMsg v3.0 protocol](https://ipmsg.org/protocol.txt), interoperable with original IPMsg and FeiQ:

- UDP port 2425 message send/receive
- TCP file transfer (port 2425)
- `IPMSG_SENDMSG` / `IPMSG_RECVMSG` / `IPMSG_FILEATTACHOPT` and other standard commands
- File receive confirmation flow (`IPMSG_GETFILEDATA` / `IPMSG_RELEASEFILES`)
- FeiQ protocol compatibility: GBK/UTF-8 encoding auto-conversion, `\0`-separator-less GETFILEDATA parsing, extended version format
- Real-time file transfer progress display, support for opening received file folder

## Download

👉 [Gitee - IPMsg Pro](https://gitee.com/masonwu21/ipmsg-pro)

## License

**Personal & Non-commercial Use**: MIT License — free to use, modify, and distribute.

**Teams over 10 people / Commercial Use**: Requires commercial authorization. Teams of up to 10 people for internal use still fall under the MIT license; exceeding 10 people or integrating into commercial products requires contacting for authorization.

© 2026 masonwu21

## Contact

- Email: support@emsoro.cn

## Support the Project

If IPMsg Pro helps you, consider donating to support ongoing development ☕

![Donate](Donate.jpg)
