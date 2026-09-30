@echo off
rem ============================================================================
rem  Build.bat - one-click build for ShineTVStudio
rem
rem    Build.bat             Debug   (default)   ->  build\debug
rem    Build.bat --release   Release            ->  build\release
rem    Build.bat --clean     drop that build dir first, then build from scratch
rem    Build.bat --help
rem
rem  WHY TWO BUILD DIRS: CMAKE_BUILD_TYPE is cached per build directory. Building
rem  Debug and Release into the same dir means every switch invalidates all
rem  objects and triggers a full rebuild both ways. Separate dirs keep each
rem  config incrementally rebuildable.
rem
rem  WHY ASCII ONLY: cmd.exe decodes a .bat with the console code page, so
rem  non-ASCII text becomes mojibake and can break parsing outright (same class
rem  of problem as a .ps1 without a UTF-8 BOM). Chinese notes live in the docs.
rem
rem  Toolchain: MSYS2 MinGW64. Override it with the env var SHINE_MINGW,
rem ============================================================================

setlocal EnableExtensions EnableDelayedExpansion

set "CFG=Debug"
set "DIRNAME=debug"
set "CLEAN=0"
set "SHINE_MINGW=%SHINE_MINGW%"
if not defined SHINE_MINGW set "SHINE_MINGW=C:\msys64\mingw64"

:parse
if "%~1"=="" goto parsed
rem Every known flag consumes itself and jumps straight back, so an unknown flag
rem falls through to the error below instead of being silently ignored.
rem A silently ignored --relase would quietly build Debug, which is exactly the
rem kind of "it did something, just not what I asked" that wastes a whole cycle.
if /I "%~1"=="--release" ( set "CFG=Release" & set "DIRNAME=release" & shift & goto parse )
if /I "%~1"=="--debug" ( set "CFG=Debug" & set "DIRNAME=debug" & shift & goto parse )
if /I "%~1"=="--clean" ( set "CLEAN=1" & shift & goto parse )
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage
if /I "%~1"=="/?" goto usage
echo [ERROR] Unknown argument: %~1
goto usage_error
:parsed

if not exist "%SHINE_MINGW%\bin\g++.exe" (
    echo [ERROR] g++ not found under "%SHINE_MINGW%\bin".
    echo         Install MSYS2 ^(https://www.msys2.org/^), or point SHINE_MINGW
    echo         at an existing MinGW64 prefix, e.g.
    echo             set SHINE_MINGW=D:\msys64\mingw64
    echo         Refusing to let CMake pick a compiler on its own: a silently
    echo         different compiler produces a binary that looks fine and
    echo         measures nothing you intended.
    exit /b 2
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake is not on PATH.
    exit /b 2
)

set "JOBS=%NUMBER_OF_PROCESSORS%"
if not defined JOBS set "JOBS=8"

rem %~dp0 ends with a backslash, and a backslash right before a closing quote
rem escapes that quote in the Windows command line. So "E:\dir\" does not
rem terminate where it looks like it does, every quote after it pairs up one
rem slot off, and the command silently arrives mangled -- e.g. -G "MinGW
rem Makefiles" turns into -G MinGW". Drop the trailing separator first.
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\build\%DIRNAME%"

echo.
echo === ShineTVStudio / %CFG% ===
echo     compiler : %SHINE_MINGW%\bin\g++.exe
for /f "tokens=1 delims=" %%v in ('"%SHINE_MINGW%\bin\g++.exe" -dumpversion 2^>nul') do set "GXXVER=%%v"
if defined GXXVER echo     version  : !GXXVER!
echo     build dir: %BUILD_DIR%
echo     jobs     : %JOBS%
echo.

if "%CLEAN%"=="1" (
    if exist "%BUILD_DIR%" (
        echo [clean] removing %BUILD_DIR%
        rmdir /s /q "%BUILD_DIR%"
        if exist "%BUILD_DIR%" (
            echo [ERROR] could not remove %BUILD_DIR%
            exit /b 3
        )
    )
)

echo === configure ===
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=%CFG% -DCMAKE_C_COMPILER=%SHINE_MINGW%\bin\gcc.exe -DCMAKE_CXX_COMPILER=%SHINE_MINGW%\bin\g++.exe
if errorlevel 1 (
    echo.
    echo [FAIL] configure returned %ERRORLEVEL%
    exit /b 1
)

echo.
echo === build ===
cmake --build "%BUILD_DIR%" --parallel %JOBS% --target ShineTVStudio
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo.
    echo [FAIL] build returned %RC%  - do NOT read any screenshot or log from
    echo        a failed build; it would describe the previous binary.
    exit /b %RC%
)

set "EXE=%BUILD_DIR%\ShineTVStudio.exe"
if not exist "%EXE%" (
    echo.
    echo [FAIL] build reported success but %EXE% does not exist
    exit /b 4
)

echo.
echo [OK] %CFG% build finished.
echo      %EXE%
for %%A in ("%EXE%") do echo      %%~zA bytes
goto :eof

:usage
echo.
echo Usage:  Build.bat [--debug / --release] [--clean]
echo.
echo   no args    Debug build    into build\debug
echo   --release  Release build  into build\release
echo   --debug    Debug build    into build\debug
echo   --clean    delete that build dir first, then build from scratch
echo   --help     this text
echo.
echo Toolchain comes from the env var SHINE_MINGW ^(default C:\msys64\mingw64^).
exit /b 0

:usage_error
echo.
echo Run "Build.bat --help" for usage.
exit /b 2
