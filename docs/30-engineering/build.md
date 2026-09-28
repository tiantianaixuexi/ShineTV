---
id: engineering.build
kind: reference
status: current
scope: build
source_of_truth:
  - CMakeLists.txt
  - scripts/package-qt.ps1
  - scripts/studio.ps1
last_verified: 2026-09-25
---

# 构建与打包

## 工具链

当前 CMake 配置声明：

- CMake 3.20 或更高；
- C++26，C++17 用于 C 源文件；
- MinGW64 GCC/G++ 16.1；
- Qt 6 Widgets；
- MinGW Makefiles。

Qt 路径由 `CMAKE_PREFIX_PATH` 指向 `C:/msys64/mingw64`。实际机器若路径不同，应在命令中显式覆盖，不要修改源码里的第三方目录。

## 配置与构建

```powershell
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 8 --target ShineTVStudio
```

修改 `CMakeLists.txt` 后必须重新 configure；新增 `.cpp` 不会自动被收集，必须手工登记到对应 target。

## CMake targets

| Target | 类型 | 关键内容 | 依赖方向 |
|---|---|---|---|
| `shine_core` | STATIC | 核心、util、net/db、llm/comfy、media/flow/visual/novel、paint/mcp、project/pipeline | 不含 Qt |
| `shine_kit` | STATIC | theme、motion、widgets、data、images、FlowCanvas | `shine_core` + Qt Widgets |
| `ShineTVStudio` | EXE | app 装配、工作区、验收开关 | `shine_core` + `shine_kit` + Qt Widgets |

第三方静态库目标包括 `shine_png`、`shine_jpeg`、`shine_sqlite`、`shine_hiredis`、`webp` 等；不要在业务模块中直接复制第三方源文件或新增平行解码器。

## 运行输出

默认目标输出：`build/ShineTVStudio.exe`。主题 JSON 在构建后复制到可执行文件旁的 `themes/`。

## 打包

`scripts/package-qt.ps1` 提供 Qt 打包流程：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-qt.ps1 `
  -BuildDir build-qt -OutDir dist/ShineTVStudio
```

脚本使用 `windeployqt6`（找不到时尝试 `windeployqt`）并复制 MinGW 运行库。打包成功不等于功能验收；还需启动产物并走对应工作区。

## 编译期反射

MinGW 分支为 C++ 源文件加入 `-freflection`。`src/util/Reflect.h` 和 `src/widget/theme/Theme.cpp` 使用静态反射序列化；修改设置或主题字段时，优先复用 `util::reflect`，不要新增手写 JSON 样板。

## 构建故障定位

1. 先确认 `cmake --version`、GCC/G++ 版本和 Qt Widgets 是否可被 `find_package` 找到。
2. 再确认新增源文件是否登记到正确 target。
3. C++ 反射错误先检查是否经过 `-freflection`；第三方 C 源文件不需要该选项。
4. 中文路径/文件错误先检查 `util::Encoding.h` 和 `util::File.h`，不要改第三方库。
5. 运行 `tools/check-layers.ps1` 区分 UI 分层违规与普通编译错误。
