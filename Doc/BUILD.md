# ShineTV Studio — Build (GCC 16.1.0)

详细约定见项目 skill：`shinetv-build`、`shinetv-thirdparty`。

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

增量编译：`cmake --build build -j 8`（改 CMakeLists 后先重新 configure）。

## 链入的第三方

mimalloc · spdlog+fmt · stdexec · libhv(hv_static) · yyjson · imgui(docking) · VisualNodeSystem

