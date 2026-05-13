# CMake Architecture

## 项目概述

- **项目名称**: AICopilotForStorage
- **版本**: 0.1.0
- **C++ 标准**: C++17 (required, no extensions)
- **CMake 最低版本**: 3.20
- **构建目标**: Windows Desktop (Win32 + DirectX 11)
- **包管理策略**: 纯 `FetchContent`，无 vcpkg/Conan

---

## 构建流程

```
CMakeLists.txt (根)
  │
  ├── cmake/Dependencies.cmake    ← 所有第三方依赖通过 FetchContent 下载
  │     ├── nlohmann_json         (ZIP, header-only)
  │     ├── cpr                   (Git shallow, 静态编译, 传递依赖 curl + zlib)
  │     ├── imgui                 (ZIP, docking 分支)
  │     └── sqlite3               (ZIP, amalgamation 单文件)
  │
  └── src/CMakeLists.txt          ← 5 个静态库 + 1 个可执行文件
        ├── core_lib              (STATIC)
        ├── db_lib                (STATIC, 内嵌 sqlite3.c)
        ├── ai_lib                (STATIC)
        ├── rule_lib              (STATIC)
        ├── ui_lib                (STATIC, 内嵌 imgui 源码)
        └── AICopilotForStorage   (EXECUTABLE, 链接所有库 + 系统 D3D)
```

---

## Target 详解

### 1. `core_lib` — 核心数据结构

| 属性 | 值 |
|---|---|
| 类型 | `STATIC` |
| 源文件 | `core/FileNode.h/.cpp`, `core/Scanner.h` |
| 公开依赖 | `nlohmann_json::nlohmann_json` |
| 作用 | 定义文件扫描的核心数据模型 `FileNode`，以及扫描器接口 `Scanner` |

### 2. `db_lib` — 数据库封装层

| 属性 | 值 |
|---|---|
| 类型 | `STATIC` |
| 源文件 | `db/Database.h/.cpp`, `${sqlite3_SOURCE_DIR}/sqlite3.c` |
| 公开依赖 | `nlohmann_json::nlohmann_json` |
| 作用 | 封装 SQLite3，提供 `Database` 类管理扫描结果的持久化存储；sqlite3 amalgamation 直接编译入库 |

### 3. `ai_lib` — AI / LLM 客户端

| 属性 | 值 |
|---|---|
| 类型 | `STATIC` |
| 源文件 | `ai/AIClient.h/.cpp`, `ai/DeepSeekProvider.h/.cpp`, `ai/PromptBuilder.h/.cpp` |
| 公开依赖 | `nlohmann_json::nlohmann_json`, `core_lib`, `cpr::cpr` |
| 作用 | 提供 LLM API 调用能力：`AIClient` 为抽象客户端，`DeepSeekProvider` 为 DeepSeek API 实现，`PromptBuilder` 负责构建 prompt；使用 CPR 发送 HTTP 请求，nlohmann/json 处理 JSON 序列化/反序列化 |

### 4. `rule_lib` — 风险分析规则引擎

| 属性 | 值 |
|---|---|
| 类型 | `STATIC` |
| 源文件 | `rule/RuleEngine.h/.cpp` |
| 公开依赖 | `core_lib` |
| 作用 | 基于 `FileNode` 数据执行风险分析规则，判断文件/目录是否需要关注 |

### 5. `ui_lib` — 图形用户界面

| 属性 | 值 |
|---|---|
| 类型 | `STATIC` |
| 源文件 | `ui/App.h/.cpp` + imgui 全套源码 (core + Win32 backend + D3D11 backend) |
| 公开依赖 | `ai_lib`, `rule_lib`, `db_lib`, `core_lib`, `d3d11.lib`, `d3dcompiler.lib` |
| 作用 | 基于 ImGui (docking 分支) + Win32 + DirectX 11 的桌面 GUI 应用；`App` 类协调所有子模块的展示和交互 |

### 6. `AICopilotForStorage` — 可执行文件

| 属性 | 值 |
|---|---|
| 类型 | `EXECUTABLE` |
| 源文件 | `main.cpp` |
| 链接依赖 (PRIVATE) | `ui_lib`, `ai_lib`, `rule_lib`, `db_lib`, `core_lib`, `d3d11.lib`, `d3dcompiler.lib`, `cpr::cpr`, `nlohmann_json::nlohmann_json` |
| 作用 | 程序入口，初始化所有模块并启动 UI 主循环 |
| 后处理 | POST_BUILD 将 `cpr.dll`, `libcurl.dll`, `zlib.dll` (若存在) 复制到输出目录 |

---

## 目标依赖图

```
AICopilotForStorage (exe)
  │
  ├── ui_lib (static) [ImGui + Win32/D3D11]
  │     ├── ai_lib (static)
  │     │     ├── core_lib (static) [FileNode, Scanner]
  │     │     ├── nlohmann_json         ← header-only JSON
  │     │     └── cpr::cpr (static)     ← HTTP client (libcurl 封装)
  │     │           └── libcurl (static) [SChannel, no OpenSSL]
  │     │                 └── zlib (static)
  │     │
  │     ├── rule_lib (static) [RuleEngine]
  │     │     └── core_lib
  │     │
  │     ├── db_lib (static) [Database + SQLite3]
  │     │     ├── nlohmann_json
  │     │     └── sqlite3 (amalgamation)  ← 嵌入式数据库
  │     │
  │     ├── core_lib
  │     ├── d3d11.lib                     ← 系统 Direct3D 11
  │     └── d3dcompiler.lib               ← 系统 D3D 着色器编译器
  │
  ├── cpr::cpr
  ├── nlohmann_json
  ├── d3d11.lib
  └── d3dcompiler.lib
```

---

## 第三方库管理

所有第三方依赖统一通过 **CMake FetchContent** 管理，在 `cmake/Dependencies.cmake` 中集中声明。

| 依赖 | 获取方式 | 源地址 | 版本 | 作用 | 构建方式 |
|---|---|---|---|---|---|
| **nlohmann/json** | FetchContent (ZIP) | `github.com/nlohmann/json` v3.11.3 | v3.11.3 | JSON 序列化/反序列化 | header-only，无需编译 |
| **CPR** (C++ Requests) | FetchContent (Git shallow) | `github.com/libcpr/cpr.git` | 1.11.1 | C++ 封装的 HTTP 客户端库 | 静态库 (STATIC)；禁止 test/example |
| **libcurl** (CPR 传递) | CPR 内建 FetchContent | (CPR 自动管理) | CPR 1.11.1 配套 | HTTP 协议底层实现 | 静态库；SChannel (Windows 原生 SSL)；Static CRT；无 OpenSSL |
| **zlib** (curl 传递) | curl 内建 FetchContent | (curl 自动管理) | curl 配套 | 数据压缩 (curl 传输压缩) | 静态库 |
| **imgui** | FetchContent (ZIP) | `github.com/ocornut/imgui` docking v1.91.8 | v1.91.8-docking | 即时模式 GUI 框架 (docking 分支) | 源码直接编译入 `ui_lib` |
| **sqlite3** | FetchContent (ZIP) | `sqlite.org/2025` amalgamation 3.48.0.0 | 3.48.0.0 (2025) | 嵌入式关系数据库 | 单文件 `sqlite3.c` 直接编译入 `db_lib` |

### 代理配置

支持通过 CMake 变量或环境变量设置 HTTP 代理：

```bash
cmake -B build -DHTTP_PROXY=http://127.0.0.1:7897
```

### CPR/curl 编译选项

```cmake
CPR_FORCE_USE_SYSTEM_CURL = OFF    # 使用 CPR 内建 curl，不依赖系统 curl
CPR_BUILD_TESTS           = OFF    # 跳过测试编译
CPR_BUILD_EXAMPLES        = OFF    # 跳过示例编译
CURL_USE_SCHANNEL         = ON     # Windows SChannel (原生 SSL)
CURL_USE_OPENSSL          = OFF    # 不使用 OpenSSL
BUILD_SHARED_LIBS         = OFF    # 全静态编译
BUILD_CURL_EXE            = OFF    # 不编译 curl 可执行文件
CURL_STATIC_CRT           = ON     # 静态 CRT 链接
```

---

## CMake 文件清单

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 根构建文件；设定项目、编译器选项、引入依赖、添加 src 子目录 |
| `cmake/Dependencies.cmake` | 第三方依赖管理；声明所有 FetchContent 依赖及编译参数 |
| `src/CMakeLists.txt` | 源码构建；定义 5 个静态库 + 1 个可执行文件的构建规则 |

---

## 编译选项

| 选项 | MSVC | 其他编译器 |
|---|---|---|
| 警告级别 | `/W4` | `-Wall -Wextra -Wpedantic` |
| 编码 | `/utf-8` | — |
| 预定义宏 | `_CRT_SECURE_NO_WARNINGS`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN` | — |

---

## 注意事项

1. **Post-Build DLL 拷贝**: 虽然所有第三方库配置为静态编译，但 `src/CMakeLists.txt` 末尾仍保留了 `cpr.dll` / `libcurl.dll` / `zlib.dll` 的 POST_BUILD 拷贝命令（带 `if exist` 判断），以便在切换为共享库构建时无需修改构建脚本。
2. **`core_lib` 仅包含头文件 `Scanner.h`**: `Scanner.h` 仅有头文件声明，无 `.cpp` 实现文件，不生成目标代码。
