---
name: shinetv-thirdparty
description: ShineTV third/ 库接入方式：mimalloc、spdlog+fmt、stdexec、libhv、yyjson、imgui、VNS。当用户要加库、改 CMake 链接、或问「日志/异步/网络用什么」时使用。
---

# 第三方库接入

原则：**全源码编入 exe**，统一 GCC，避免混用 MSVC 静态库。列表见根 `CMakeLists.txt`。

## 库一览

| 库 | 接入方式 | 宏 / 注意 |
|----|----------|-----------|
| **mimalloc** | 编 `third/mimalloc/src/static.c`；`main` 里 `mi_process_init()` | `MI_MALLOC_OVERRIDE=0`（不强制覆盖 CRT，按需 `mi_malloc`） |
| **fmt** | header-only | `FMT_HEADER_ONLY`；include 路径 `third/fmt` |
| **spdlog** | header-only + 外部 fmt | `SPDLOG_FMT_EXTERNAL`；include **父目录 `third/`**，写 `#include <spdlog/spdlog.h>` |
| **stdexec** | header-only | include `third/stdexec/include`；用 `exec::start_detached` + `exec::static_thread_pool` |
| **libhv** | `add_subdirectory` → `hv_static` | `HV_STATICLIB`；`BUILD_SHARED=OFF` `WITH_HTTP_SERVER=**ON**`（P7.2 MCP）`SHINE_SKIP_LIBHV_RC=ON`；**HTTPS 走 WITH_WINTLS（Schannel）**，`WITH_OPENSSL` 保持 OFF（与 `libcrypto.a` 符号冲突）；服务端头文件 `#include <hv/HttpServer.h>` |
| **yyjson** | 编 `yyjson.c` | 设置 + Comfy JSON |
| **imgui** | docking 分支源文件直编 | win32 + dx11 backend |
| **VisualNodeSystem** | 源文件列表直编；业务经 `src/graph/GraphHost` | jsoncpp + GLM；P2 起用画布；Node 析构 protected |
| **zmij / zlib** | 已在 third，按需 | 目前未强制链入业务 |
| **hiredis** | `shine_hiredis` 静态库（`src/db/redis/`） | 不 `add_subdirectory`；业务经 `db/Db.h` / `db/redis/Redis.h` |
| **sqlite3** | `shine_sqlite` 静态库（`third/sqlite/sqlite3.c` amalgamation） | `SQLITE_ENABLE_FTS5`；路径用 `std::filesystem::path` |

| **function2** | header-only `third/function2` | include `function2/function2.hpp`；WS 高频监听用 `fu2::function`（可拷贝+SBO） |
| **ImAnim** | `third/ImAnim/`（ImGui 动画库，**已 vendored，CMake 尚未编入 exe**） | 以后做 UI 动效/补间再 `add_executable` 接入；先读 `third/ImAnim/docs/quickstart.md`，**不要**在业务里手写一套动画曲线 |

## 使用入口

| 需求 | 用法 |
|------|------|
| 日志 | `#include "core/Log.h"` → `log::Info("{}", x)` |
| 后台任务 | `#include "core/Async.h"` → `RunOnWorker` / `PostToUi` / `DrainUiQueue`（参数已是 `std::move_only_function`） |
| 异步回调类型 | `comfy::*Cb` / `gallery::ScanAsync` / `media::VideoThumbCb` → **`std::move_only_function`**（GCC 16 可用） |
| HTTP/WS | `#include "net/HttpClient.h"` 或 `comfy/ComfyHttp.h`（业务语境包装） |
| JSON | `#include <yyjson.h>` |
| 分配 | `#include <mimalloc.h>` → `mi_malloc` / `mi_free`（大缓冲） |

## 禁止

- 用 `printf`/`std::cout` 做正式日志
- 用 `vsnprintf` 拼业务字符串（用 fmt）
- 手写线程池堆 Comfy/图片业务（用 stdexec）
- 再 `add_subdirectory(third/mimalloc)`（已手编 static.c，会重复符号）
- 把 `third/spdlog` 当 include 根（头文件期望 `spdlog/` 前缀）

## libhv 命名空间

`HttpRequest` / `HttpResponse` / `http_client_send` 在**全局**命名空间，不是 `hv::`。

WebSocket：`hv::WebSocketClient` + `hv::EventLoopThread`（见 `ComfySocket.cpp`）。

## 新增第三方库步骤

1. 放入 `third/<name>`
2. 根 CMake：include 路径 + `target_sources` 或 `add_subdirectory`
3. 若静态：注意与 `-static-libstdc++` 兼容，优先同编译器源码编入
4. 更新本表与 `AGENTS.md` 技术栈表
5. 在业务里包一层（如 `core/` 或 `comfy/`），避免 UI 直接依赖 third 细节

## 相关 skill

`shinetv-build`（编译错误）、`shinetv-comfy`（libhv/yyjson/stdexec 用例）。
