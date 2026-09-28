param(
    [string]$BuildDir = "build-qt",
    [string]$OutDir = "dist/ShineTVStudio"
)
# ShineTV Studio (Qt) 打包
# 产物: dist/ShineTVStudio/ShineTVStudio.exe + Qt 运行库
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"

cmake -S . -B $BuildDir -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe `
  -DCMAKE_BUILD_TYPE=Release `
  -DSHINE_QT_UI=ON
if ($LASTEXITCODE -ne 0) { throw "configure failed: $LASTEXITCODE" }

cmake --build $BuildDir -j 8 --target ShineTVStudio
if ($LASTEXITCODE -ne 0) { throw "build failed: $LASTEXITCODE" }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item "$BuildDir/ShineTVStudio.exe" "$OutDir/ShineTVStudio.exe" -Force

$windeploy = "C:\msys64\mingw64\bin\windeployqt6.exe"
if (-not (Test-Path $windeploy)) { $windeploy = "C:\msys64\mingw64\bin\windeployqt.exe" }
& $windeploy --release --compiler-runtime --no-translations "$OutDir/ShineTVStudio.exe"

# MinGW 运行时（windeployqt 不一定带上）
foreach ($dll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    $src = "C:\msys64\mingw64\bin\$dll"
    if (Test-Path $src) { Copy-Item $src $OutDir -Force }
}

Write-Host "打包完成: $OutDir\ShineTVStudio.exe"
