# 倍信 (IPMsg Pro) v1.0.0

> 局域网即时通讯的现代化选择 — 兼容飞秋 & IPMsg，界面更美，体验更好

基于 [TauriCPP](https://github.com/masonwu21/TauriCPP) 框架和 [IPMsg v3.0 协议](https://ipmsg.org/) 实现的局域网即时通讯应用。与飞秋（FeiQ）和飞鸽传书（IPMsg）完全互通，同时提供更现代化的用户体验。

## 为什么选择倍信？

还在用飞秋那个 2008 年的界面？还在忍受飞鸽传书的简陋聊天窗口？倍信给你同样的局域网通讯能力，但体验完全不同：

| 特性 | 飞秋 (FeiQ) | 飞鸽传书 (IPMsg) | **倍信 (IPMsg Pro)** |
|---|:---|:---|:---|
| 界面风格 | Delphi 古典风格 | Win32 原生简陋界面 | **类微信三栏现代界面** |
| 消息送达回执 | 仅文件传输有 | 仅文件传输有 | **文字消息送达回执** |
| 图片发送 | 不支持 | 不支持 | **图片预览+缩略图展示** |
| 文件传输进度 | 简陋进度条 | 无进度显示 | **实时进度+打开文件夹** |
| 消息历史 | 无持久化 | 无持久化 | **SQLite 持久存储+搜索** |
| 用户昵称 | 登录名即昵称 | 登录名即昵称 | **自定义昵称+分组** |
| 多网段发现 | 手动加好友 | 手动加好友 | **IP网段自动扫描发现** |
| 编码兼容 | 仅 GBK | 仅 GBK/Shift-JIS | **GBK/UTF-8 自动转换** |
| 系统依赖 | Delphi 运行时 | 无 | **单 exe，无安装依赖** |
| DPI 适配 | 不支持 | 不支持 | **高 DPI 自适应** |
| 协议互通 | FeiQ 协议 | IPMsg v3 协议 | **同时兼容两者** |
| 传输协议 | UDP+TCP | UDP+TCP | **UDP+TCP，完全兼容** |
| 开源状态 | 不开源 | 开源 (GPL) | **开源 (个人MIT)** |

**核心优势**：和飞秋在同一网络中直接互通，无需对方安装倍信。对方用飞秋发消息你能收到，你发消息飞秋也能收到。但你自己用的是现代化的聊天体验。

## 功能特性

- **用户自动发现** — UDP 广播自动发现局域网用户，支持多网段 IP 范围扫描
- **文字消息** — 实时收发文字消息，送达回执确认
- **图片发送** — 发送图片前预览确认，接收后缩略图展示
- **文件传输** — 发送/接收文件确认流程，传输进度实时显示，一键打开文件夹
- **类微信界面** — 三栏布局：左侧会话列表、中间用户列表、右侧聊天面板
- **数据持久化** — SQLite 存储消息历史，配置信息本地保存
- **多网段发现** — 配置 IP 范围后自动扫描，发现不同子网的用户
- **统一日志系统** — Debug 版自动开启日志，Release 版静默运行

## 技术特点

### 架构

- **前后端分离** — C++17 后端处理网络通讯，React 前端渲染 UI，通过 Bridge 通信
- **单 exe 部署** — 前端资源打包嵌入 exe，零安装依赖，复制即用
- **WebView2 渲染** — 利用系统自带 WebView2 运行时，无需额外安装浏览器引擎
- **静态 CRT 链接** — Release 版使用 `/MT` 静态链接，不依赖 MSVC 运行时 DLL

### 协议兼容

- **双向互通** — 与飞秋和 IPMsg v3 完全互通，无需对方更换软件
- **GBK/UTF-8 双模** — 发送时自动检测目标客户端编码偏好，智能选择 UTF-8 或 GBK
- **CAPUTF8OPT 扩展** — 支持 IPMsg 扩展协议，携带完整用户信息（昵称、分组）
- **飞秋特殊兼容** — 针对飞秋协议的多个差异点做了适配：
  - 飞秋不支持 UTF8OPT 标记中文文本 → 自动降级为 GBK 编码
  - 飞秋 GETFILEDATA 请求不含 `\0` 分隔符 → 兼容解析
  - 飞秋扩展版本号格式 → 正确识别

### 安全与性能

- **无外部服务** — 纯局域网通讯，数据不经过任何云端服务器
- **UDP 消息 + TCP 文件** — 消息用 UDP 即时送达，文件用 TCP 确保可靠传输
- **高效编码转换** — 使用 Windows API 直接转换 GBK↔UTF-8，零第三方依赖

## 技术栈

| 层 | 技术 |
|---|---|
| 前端 | React + TypeScript + Tailwind CSS + Vite |
| 后端 | C++17 + Win32 + WebView2 |
| 框架 | TauriCPP (轻量 Tauri 替代，单 exe 无安装) |
| 协议 | IPMsg v3.0 (UDP 2425) + TCP 文件传输 |
| 数据库 | SQLite3 |
| 通信 | Bridge (前后端 JSON-RPC) |

## 项目结构

```
IPMsgPro/
├── src/                    # C++ 后端
│   ├── main.cpp           # 应用入口，WebView2 窗口管理
│   ├── ipmsg/             # IPMsg 协议实现（移植自 ipmsg-master）
│   │   ├── msgmng.cpp     # 核心协议：编解码、收发、编码转换
│   │   ├── network.cpp    # UDP/TCP 网络层
│   │   └── protocol.h     # 协议常量定义
│   ├── bridge/            # 前后端桥接命令处理
│   ├── database/          # SQLite 消息存储
│   └── file/              # TCP 文件传输管理
├── frontend/              # React 前端
│   └── src/
│       ├── components/    # UI 组件（ChatPanel, UserList, Settings...）
│       ├── stores/        # Zustand 状态管理
│       ├── services/      # 后端桥接服务 + IndexedDB 配置
│       └── types/         # TypeScript 类型定义
├── TauriCPP/              # TauriCPP 框架（子模块）
├── resources/             # 应用图标、资源
├── doc/                   # 文档与宣传资料
└── CMakeLists.txt         # CMake 构建配置
```

## 构建方式

### 前置依赖

- Visual Studio 2022（含 C++ 桌面开发工作负载）
- CMake 3.15+
- Python 3（用于资源打包）
- Node.js 18+
- vcpkg（安装 WebView2 SDK）

### 构建步骤

```powershell
# 1. 安装前端依赖
cd frontend
npm install

# 2. 构建前端
npm run build

# 3. 构建 C++ 后端
cd ..
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 输出: build/Release/IPMsgPro.exe
```

或使用一键构建脚本：

```powershell
.\build.ps1
```

## 协议兼容详情

兼容 [IPMsg v3.0 协议](https://ipmsg.org/protocol.txt)，可与原版 IPMsg 及飞秋（FeiQ）互通：

- UDP 2425 端口消息收发
- TCP 文件传输（端口 2425）
- `IPMSG_SENDMSG` / `IPMSG_RECVMSG` / `IPMSG_FILEATTACHOPT` 等标准命令
- 文件接收确认流程（`IPMSG_GETFILEDATA` / `IPMSG_RELEASEFILES`）
- 飞秋协议兼容：GBK/UTF-8 编码自动转换、无 `\0` 分隔符的 GETFILEDATA 解析、扩展版本号格式
- 文件传输进度实时显示，支持打开接收文件所在文件夹

## License

**个人及非商业用途**：MIT License，自由使用、修改、分发。

**10人以上团队/商业用途**：需获得商业授权许可。10人以内的团队内部使用仍适用 MIT 协议；超过10人或在商业产品中集成使用，请联系获取授权。

© 2026 masonwu21

## 联系方式

- 邮箱：support@emsoro.cn

## 支持项目

如果倍信对你有帮助，欢迎打赏支持持续开发 ☕

![Donate](Donate.jpg)
