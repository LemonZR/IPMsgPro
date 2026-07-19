# 倍信 (IPMsg Pro) v1.1.0

基于 [TauriCPP](https://github.com/masonwu21/TauriCPP) 框架和 [ipmsg-master](https://ipmsg.org/) 协议实现的局域网即时通讯应用，兼容飞秋和IPMsg v3.0 协议（UDP 2425 端口）。

## 功能特性

- **用户自动发现** — UDP 广播自动发现局域网用户，支持多网段配置
- **文字消息** — 实时收发文字消息，送达回执
- **图片发送** — 发送图片前预览确认，接收后缩略图展示
- **文件传输** — 发送/接收文件确认流程，传输进度实时显示
- **类微信界面** — 三栏布局：左侧会话列表、中间用户列表、右侧聊天面板
- **数据持久化** — SQLite 存储消息历史，配置信息本地保存
- **新消息提示音** — 收到新文字消息或文件接收请求时播放提示音，可在「设置」中开关（提示音已作为资源嵌入 exe，运行时自动提取播放）

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
- 文件传输进度实时显示，支持打开接收文件所在文件夹

## License

MIT License © 2026 masonwu21
