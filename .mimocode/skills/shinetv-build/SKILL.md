---
name: shinetv-build
description: ShineTV 当前 GCC/MSYS2/CMake/Qt 构建、目标划分、编译故障定位与打包。
---

# ShineTV 构建

先读：[构建说明](../../../docs/30-engineering/build.md)、[检查脚本](../../../docs/30-engineering/checks.md)。

## 工具链

- CMake 3.20+；C++26，C 源文件 C17。
- MSYS2 MinGW64 GCC/G++ 16.1，路径 `C:/msys64/mingw64/bin`。
- Qt 6 Widgets；CMake 生成 `shine_core`、`shine_kit`、`ShineTVStudio`。

## 命令

```powershell
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 8 --target ShineTVStudio
```

`powershell -File scripts/studio.ps1 build` 是同一构建流程的包装。修改 `CMakeLists.txt` 后先 configure；新增 `.cpp` 必须登记到对应 target。

## 依赖边界

- 业务层只用 `net::HttpClient`、`core/Log`、`core/Async`、`util/Reflect` 等适配入口。
- `shine_core` 不链接 Qt；`shine_kit` 和可执行程序才使用 Qt。
- C++ 源文件由 MinGW 分支加 `-freflection`；第三方 C 文件不需要。
- 静态库源清单逐项写在根 CMake，不凭记忆补第三方文件。

## 故障定位

1. 先跑 `tools/check-layers.ps1` 区分分层错误。
2. 检查 Qt `find_package`、GCC 版本和 CMake cache。
3. 检查新增源文件是否在 CMake 列表。
4. 中文路径/编码问题查 `util::Encoding.h`、`util::File.h`。
5. 运行真实目标或对应自检；编译成功不等于交互成功。
