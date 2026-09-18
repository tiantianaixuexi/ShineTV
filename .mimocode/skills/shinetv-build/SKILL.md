---
name: shinetv-build
description: ShineTV 使用 GCC 16.1.0 MinGW 构建、CMake 选项、常见编译错误。当用户要编译、配置 build、改 CMakeLists、或报「找不到编译器/链接错误」时使用。
---

# ShineTV 构建（GCC 16.1.0）

## 硬性工具链

- **只用** MSYS2 MinGW64 GCC 16.1.0，路径 `C:/msys64/mingw64/bin`
- 禁止 Clang / MSVC / 外部 Ninja（除非用户明确要求）

## 一键构建

```powershell
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 8
# 输出: build\ShineTVStudio.exe
```

增量：直接 `cmake --build build -j 8`。改 CMakeLists 后需重新 configure。

## CMake 要点（根 `CMakeLists.txt`）

| 项 | 值 |
|----|-----|
| 标准 | C++26 / C17 |
| 宏 | `UNICODE` `NOMINMAX` `WIN32_LEAN_AND_MEAN` `FMT_HEADER_ONLY` `SPDLOG_FMT_EXTERNAL` `HV_STATICLIB` `MI_MALLOC_OVERRIDE=0` |
| 链接 | `hv_static` + d3d11/dxgi/dwmapi + secur32/crypt32/winmm/iphlpapi/ws2_32 |
| 链接选项 | `-municode -static -static-libgcc -static-libstdc++` |
| libhv | `add_subdirectory(third/libhv)`，`BUILD_SHARED=OFF`，`WITH_HTTP_SERVER=OFF`，`SHINE_SKIP_LIBHV_RC=ON` |
| mimalloc | 直接编 `third/mimalloc/src/static.c`，**不要**再 add_subdirectory |

## 常见错误

| 现象 | 处理 |
|------|------|
| `spdlog/xxx.h: No such file` | include 需含 `third/`（父目录），不是 `third/spdlog` |
| `hv::HttpRequest` 不存在 | libhv 的 `HttpRequest`/`HttpResponse` 在**全局命名空间** |
| `stdexec::start_detached` 废弃 | `#include <exec/start_detached.hpp>`，用 `exec::start_detached` |
| `yyjson_write(val)` 参数错 | 写值用 `yyjson_val_write` |
| ImGui 中文方框 | 检查 `Fonts.cpp` 是否加载 msyh + Chinese glyph range |
| libhv RC 规则炸 | 必须保持 `SHINE_SKIP_LIBHV_RC ON` |
| `ImMin`/`ImMax` 未定义 | 用 `std::min`/`std::max`，或 include `imgui_internal.h` |

## 新源文件

在 `add_executable(ShineTVStudio ...)` 列表追加；头文件目录已含 `src/`、`third/`、libhv 各子目录。

## 相关 skill

`shinetv-thirdparty` — 库接入细节；`shinetv-structure` — 文件放哪。
