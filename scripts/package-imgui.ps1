param(
    [string]$BuildDir = "build-pkg",
    [string]$OutDir = "dist/ShineTVStudio"
)
# ShineTV Studio (Dear ImGui) 打包
# 产物: dist/ShineTVStudio/ShineTVStudio.exe + themes/ + MinGW 运行时
#
# 本脚本当年替掉的是 Qt 时代的打包脚本（已随 Qt 树一起删除，提交 ddab612）。
# 记下它的两个实质差别，因为这两个坑都「编译得过、跑得起来、只是结果不对」：
#   1. 旧脚本传 -DSHINE_QT_UI=ON —— 这个开关在 CMakeLists 里**从来不存在**
#      （真名是 SHINE_UI_QT，且 P7 已连同 Qt 树一起删掉），所以它一直在静默地
#      配出一个 ImGui 版再叫 windeployqt 去部署 Qt 运行库。
#   2. 不需要 windeployqt：ImGui 前端是静态编进 exe 的，运行期只要系统
#      opengl32.dll（系统自带）+ MinGW 的 libgcc/libstdc++/libwinpthread。
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"

cmake -S . -B $BuildDir -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe `
  -DCMAKE_BUILD_TYPE=Release `
  -DSHINE_UI_IMGUI=ON
if ($LASTEXITCODE -ne 0) { throw "configure failed: $LASTEXITCODE" }

cmake --build $BuildDir -j 8 --target ShineTVStudio
if ($LASTEXITCODE -ne 0) { throw "build failed: $LASTEXITCODE" }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item "$BuildDir/ShineTVStudio.exe" "$OutDir/ShineTVStudio.exe" -Force

# 5 套主题 JSON：AppEntry 的 LoadStartupTheme 从 <exe目录>/themes 读
# （SHINE_THEME_DIR 可覆盖）。漏拷的后果不报错，只是主题静默落回内置默认值。
if (Test-Path "$OutDir/themes") { Remove-Item -Recurse -Force "$OutDir/themes" }
Copy-Item -Recurse "$BuildDir/themes" "$OutDir/themes" -Force

# MinGW 运行时。opengl32 / user32 / gdi32 / dwmapi 都是系统 DLL，不用带。
foreach ($dll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    $src = "C:\msys64\mingw64\bin\$dll"
    if (Test-Path $src) {
        Copy-Item $src $OutDir -Force
    } else {
        Write-Warning "missing MinGW runtime: $dll"
    }
}

$size = (Get-Item "$OutDir/ShineTVStudio.exe").Length
Write-Host ("打包完成: {0}  (exe {1:N0} B = {2:N1} MB)" -f "$OutDir\ShineTVStudio.exe", $size, ($size / 1MB))
