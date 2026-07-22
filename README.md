# 倍信 (IPMsg Pro) v1.2.2

基于 [TauriCPP](https://github.com/masonwu21/TauriCPP) 框架和 [ipmsg-master](https://ipmsg.org/) 协议实现的局域网即时通讯应用，兼容飞秋和IPMsg v3.0 协议（UDP 2425 端口）。

## 功能特性

- **用户自动发现** — UDP 广播自动发现局域网用户，支持多网段配置
- **文字消息** — 实时收发文字消息，送达回执
- **图片发送** — 发送图片前预览确认，接收后缩略图展示
- **文件传输** — 发送/接收文件确认流程，传输进度实时显示
- **类微信界面** — 三栏布局：左侧会话列表、中间用户列表、右侧聊天面板
- **数据持久化** — SQLite 存储消息历史，配置信息本地保存
- **新消息提示音** — 收到新文字消息或文件接收请求时播放提示音，可在「设置」中开关（提示音已作为资源嵌入 exe，运行时自动提取播放）
- **表情支持** — 聊天输入框可插入 emoji 表情，基于雪碧图（`emoji.png`）+ 定位数据（`emoji_positon.less`）实现

## 技术栈

| 层 | 技术 |
|---|---|
| 前端 | React + TypeScript + Tailwind CSS + Vite |
| 后端 | C++17 + Win32 + WebView2 |
| 框架 | TauriCPP (轻量 Tauri 替代，单 exe 无安装) |
| 协议 | IPMsg v3.0 (UDP 2425) + TCP 文件传输 |
| 数据库 | SQLite3 |

## 项目结构

```
IPMsgPro/
├── src/                    # C++ 后端
│   ├── main.cpp           # 应用入口
│   ├── ipmsg/             # IPMsg 协议实现（移植自 ipmsg-master）
│   ├── bridge/            # 前后端桥接命令处理
│   ├── database/          # SQLite 消息存储
│   └── file/              # TCP 文件传输管理
├── frontend/              # React 前端
│   └── src/
│       ├── components/    # UI 组件
│       ├── stores/        # Zustand 状态管理
│       ├── services/      # 后端桥接服务
│       ├── assets/        # 静态资源（emoji 雪碧图 emoji.png 与定位样式 emoji_positon.less）
│       ├── emojiData.ts   # 由 assets/emoji_positon.less 生成的表情数据（scripts/gen_emoji_ts.cjs）
│       └── types/         # TypeScript 类型定义
├── TauriCPP/              # TauriCPP 框架（子模块）
├── resources/             # 应用图标、通知提示音(embedded)等资源
└── CMakeLists.txt         # CMake 构建配置
```

## 构建方式

### 前置依赖

- Visual Studio 2022（含 C++ 桌面开发工作负载）
- CMake 3.15+
- Python 3（用于资源打包）
- Node.js 18+

### 构建步骤

```powershell
# 一键构建（自动安装前端依赖、构建前端、编译 C++）
.\build.ps1 -Arch x64          # 编译 x64 → build_x64/Release/IPMsgPro.exe
.\build.ps1 -Arch x86          # 编译 x86 → build_x86/Release/IPMsgPro_X86.exe

# 其他选项
.\build.ps1 -Arch x64 -Config Debug   # Debug 模式
.\build.ps1 -Arch x86 -Clean          # 清理重编
.\build.ps1 -Arch x64 -Run            # 编译完自动运行
.\build.ps1 -Arch x64 -SkipFrontend   # 跳过前端构建（前端未改动时）
```

### 手动构建

```powershell
# 1. 安装前端依赖并构建
cd frontend
npm install
npm run build

# 2. 编译 C++（x64 / x86 二选一或都编译）
cd ..
cmake -B build_x64 -G "Visual Studio 17 2022" -A x64
cmake --build build_x64 --config Release

cmake -B build_x86 -G "Visual Studio 17 2022" -A Win32
cmake --build build_x86 --config Release

# 输出:
#   build_x64/Release/IPMsgPro.exe
#   build_x86/Release/IPMsgPro_X86.exe
```

## 协议兼容

兼容 [IPMsg v3.0 协议](https://ipmsg.org/protocol.txt)，可与原版 IPMsg 及飞秋（FeiQ）互通：

- UDP 2425 端口消息收发
- TCP 文件传输（端口 2425）
- `IPMSG_SENDMSG` / `IPMSG_RECVMSG` / `IPMSG_FILEATTACHOPT` 等标准命令
- 文件接收确认流程（`IPMSG_GETFILEDATA` / `IPMSG_RELEASEFILES`）
- 飞秋协议兼容：GBK/UTF-8 编码自动转换、无 `\0` 分隔符的 GETFILEDATA 解析、扩展版本号格式
- 文件传输进度实时显示，发送完成后进度保持在 100% 并显示「发送成功」，支持打开接收文件所在文件夹

## 更新日志

### v1.2.2
- **修复飞秋列表中中文昵称/群组名乱码**
  - 本地用户信息（昵称、群组等）以 UTF-8 存储，广播报文在未携带 `IPMSG_UTF8OPT` 时统一编码为 GBK 发送，飞秋（FeiQ）可正确显示中文名
  - 文本消息与文件消息正文统一在 `MakeMsg` 内完成 UTF-8 → GBK 编码，避免重复转换
  - **修复“打开文件夹”跳到桌面**：接收文件后打开所在文件夹时，UTF-8 路径未正确转为 UTF-16 导致中文路径失效，改用 `MultiByteToWideChar(CP_UTF8)` 正确转换
  - **文件传输崩溃防护与追踪日志**：文件收/发均运行于 detached 线程，未捕获异常会触发 `std::terminate` 直接崩溃且无日志。现已在 `HandleClientConnection`/`SendFileThread`/`RecvFileThread` 入口及前端事件回调 `bridge_->Emit`（含 WebView2 回调）处包裹 try/catch 并记录异常；并在 `WinMain` 安装 `SetUnhandledExceptionFilter` + `std::set_terminate`，将 SEH 异常/未捕获 C++ 异常写入 `ipmsg_gui_debug.log`，便于定位中文文件名等场景的崩溃根因
- **日志入口统一**：将分散的 `WriteTransferLog`（`FILE_XFER`）、`WriteDebugLog`（`BRIDGE`）、`MsgLog`（`MSGMNG`）包装函数以及各模块中裸写的 `std::cout`/`std::cerr` 全部收口到唯一的 `ipmsg::LogMessage(tag, level, msg)` 入口，日志统一按 `[时间][模块][级别] 消息` 格式输出到 `ipmsg_gui_debug.log`，便于统一追踪与排查。

### v1.2.0
- **文件传输进度显示优化**
  - 发送/接收过程中只更新对应气泡的进度条，不再整体刷新整个对话列表（解决对话记录不断闪烁的问题）
  - 发送完成后进度条保持在 100%，不再回落到 0%
  - 兼容飞秋分片/续传（GETFILEDATA）请求，避免续传重新从 0% 上报导致进度归零
- **统一日志系统**
  - 将原先分散的 `ipmsgpro.log`、`msgmng.log` 与 `ipmsg_gui_debug.log` 合并为单一文件 `ipmsg_gui_debug.log`
  - `std::cout` / `std::cerr` 的输出也重定向进该文件
  - 文件在每次启动应用时清空重建，日志按 `[时间] [模块] [级别] 消息` 格式输出（`IPMSGPRO` / `MSGMNG` / `FILE_XFER` / `BRIDGE` 标签区分来源）
  - 日志目录：`%LOCALAPPDATA%\.ipmsgpro\ipmsg_gui_debug.log`（默认端口；自定义端口为 `.ipmsgpro_<端口>`）

## 打赏 (Donate)

如果这个项目对你有帮助，欢迎请作者喝杯咖啡 ☕

![打赏](Donate.jpg)

## License

本项目采用 **MIT 许可证**，仅供个人非商业用途免费使用。

- **个人使用**：遵循 MIT 许可证，可自由使用、修改、分发。
- **商业使用**：如需用于商业用途，须事先获得作者授权。

如需商业授权，请联系：**support@emsoro.cn**

MIT License © 2026 masonwu21（个人）
