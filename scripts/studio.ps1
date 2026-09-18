# scripts/studio.ps1 -- ShineTV Studio process + progress control for AI automation.
#
# MUST stay pure ASCII: Windows PowerShell 5.1 reads BOM-less UTF-8 as ANSI and
# will break on Chinese source (see scripts/capture_window.ps1).
# Progress files are UTF-8 Chinese; this script reads them as UTF8 and matches
# ASCII-only patterns (table pipes, P-ids, checkboxes).
#
# Usage (from project root E:\c++\ShineTV):
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\studio.ps1 status
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\studio.ps1 start
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\studio.ps1 stop
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\studio.ps1 restart
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\studio.ps1 progress
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\studio.ps1 build
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\studio.ps1 all
#
# Commands:
#   status    - print RUNNING/STOPPED + exe/build/progress one-liners
#   start     - start ShineTVStudio.exe if not running (idempotent)
#   stop      - CloseMainWindow then Stop-Process -Force if needed
#   restart   - stop + start
#   progress  - parse Plan/PROGRESS.md + novel PROGRESS.md
#   build     - cmake configure (if needed) + incremental build
#   all       - status + progress
#
# Exit codes:
#   status: 0 = RUNNING, 1 = STOPPED
#   start/restart: 0 = running after call, 1 = failed
#   stop: 0 = not running after call, 1 = still running
#   progress/build/all/help: 0 on success, 1 on failure
#
# Output tokens (stable for automation):
#   STATUS RUNNING|STOPPED pid=... responding=...
#   EXE path=... exists=... size=... mtime=...
#   BUILD configured=... gcc=...
#   PROGRESS ...
#   NEXT ...
#   NOVEL ...
#   ACTION start|stop|restart result=...

param(
    [Parameter(Position = 0)]
    [ValidateSet("status", "start", "stop", "restart", "progress", "build", "all", "help")]
    [string]$Cmd = "status",

    [string]$Exe = "",
    [int]$StartWaitSec = 8,
    [int]$StopWaitSec = 6,
    [int]$WindowWaitSec = 15,
    [int]$Jobs = 8
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $ScriptDir
$WindowTitle = "ShineTV Studio"
$ProcessName = "ShineTVStudio"
$ProgressPath = Join-Path $Root "Plan\PROGRESS.md"
$NovelProgressPath = Join-Path $Root "docs\compose\plans\novel-agent\PROGRESS.md"
$MinGwBin = "C:\msys64\mingw64\bin"
$Gcc = Join-Path $MinGwBin "gcc.exe"
$Gxx = Join-Path $MinGwBin "g++.exe"
$Make = Join-Path $MinGwBin "mingw32-make.exe"
$BuildDir = Join-Path $Root "build"

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $BuildDir "ShineTVStudio.exe"
}

function Write-KV {
    param([string]$Key, [string]$Value)
    Write-Output ("{0} {1}" -f $Key, $Value)
}

function Get-StudioProcesses {
    return @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
}

function Get-WindowHandle {
    $sig = @"
using System;
using System.Runtime.InteropServices;
public class StudioWin32 {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
}
"@
    if (-not ("StudioWin32" -as [type])) {
        Add-Type -TypeDefinition $sig -ErrorAction SilentlyContinue | Out-Null
    }
    return [StudioWin32]::FindWindow($null, $WindowTitle)
}

function Get-ExeInfo {
    $exists = Test-Path -LiteralPath $Exe
    if (-not $exists) {
        return @{ Exists = $false; Path = $Exe; Size = 0; MTime = "" }
    }
    $item = Get-Item -LiteralPath $Exe
    return @{
        Exists = $true
        Path   = $item.FullName
        Size   = $item.Length
        MTime  = $item.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
    }
}

function Get-ProgressSummary {
    $overview = New-Object System.Collections.Generic.List[string]
    $next = New-Object System.Collections.Generic.List[string]

    if (-not (Test-Path -LiteralPath $ProgressPath)) {
        $overview.Add("missing path=" + $ProgressPath)
        return @{ Overview = $overview; Next = $next }
    }

    # Force UTF8 so Chinese table cells in PROGRESS.md are not mojibake.
    $text = [System.IO.File]::ReadAllLines($ProgressPath, [System.Text.Encoding]::UTF8)

    # PROGRESS.md structure (ASCII-safe section index):
    #   first  ## heading  -> overview
    #   second ## heading  -> "now do which" / next tasks
    #   later  ## headings -> per-category detail
    $section = -1
    foreach ($raw in $text) {
        if ($raw -match '^##\s+') {
            $section++
            continue
        }

        if ($section -eq 0) {
            # Overview table rows start with | **P3** / | **G** / | **novel-ish**
            if ($raw -match '^\|\s*\*\*(P\d|G)\*\*') {
                $cells = @($raw -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
                if ($cells.Count -ge 5) {
                    $overview.Add(($cells -join " | "))
                }
            }
            elseif ($raw -match '^\|.*\*\*G\*\*') {
                $cells = @($raw -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
                if ($cells.Count -ge 5) { $overview.Add(($cells -join " | ")) }
            }
            continue
        }

        if ($section -eq 1) {
            $t = $raw.Trim()
            if ($t -eq '') { continue }
            if ($t -match '^-{3,}') { continue }
            if ($t -match '^\d+\.' -or $t -match '^-' -or $t -match 'P\d' -or $t -match 'G-S' -or $t -match 'P10') {
                if ($next.Count -lt 12) { $next.Add($t) }
            }
            continue
        }
    }

    # Fallback if section parse missed overview rows.
    if ($overview.Count -eq 0) {
        foreach ($raw in $text) {
            if ($raw -match '^\|\s*\*\*(P\d|G)\*\*') {
                $cells = @($raw -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
                if ($cells.Count -ge 5) { $overview.Add(($cells -join " | ")) }
            }
        }
    }

    if ($next.Count -eq 0) {
        foreach ($raw in $text) {
            if ($raw -match 'P5\.7|P10\.4|task/P6|task/P8|P6-|P8-') {
                $t = $raw.Trim()
                if ($t -ne '' -and $next.Count -lt 8) { $next.Add($t) }
            }
        }
    }

    return @{ Overview = $overview; Next = $next }
}

function Get-NovelProgressSummary {
    $out = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath $NovelProgressPath)) {
        $out.Add("missing path=" + $NovelProgressPath)
        return $out
    }
    $text = [System.IO.File]::ReadAllLines($NovelProgressPath, [System.Text.Encoding]::UTF8)
    foreach ($raw in $text) {
        if ($raw -match '^\|\s*P10\.') {
            $cells = @($raw -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
            if ($cells.Count -ge 3) { $out.Add(($cells -join " | ")) }
        }
        elseif ($raw -match 'Garnet|LLM Key|P10\.4') {
            $t = $raw.Trim()
            if ($t -ne '' -and $out.Count -lt 12) { $out.Add($t) }
        }
    }
    return $out
}

function Show-Status {
    $procs = Get-StudioProcesses
    $exeInfo = Get-ExeInfo
    $configured = Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt")
    $gccOk = Test-Path -LiteralPath $Gcc

    if ($procs.Count -gt 0) {
        $p = $procs[0]
        $responding = $true
        try { $responding = $p.Responding } catch { $responding = $true }
        $startAt = ""
        try { if ($p.StartTime) { $startAt = $p.StartTime.ToString("yyyy-MM-dd HH:mm:ss") } } catch {}
        Write-KV "STATUS" ("RUNNING pid={0} name={1} responding={2} start={3} count={4}" -f `
            $p.Id, $ProcessName, $responding, $startAt, $procs.Count)
        Write-KV "EXE" ("path={0} exists={1} size={2} mtime={3}" -f `
            $exeInfo.Path, $exeInfo.Exists, $exeInfo.Size, $exeInfo.MTime)
        Write-KV "BUILD" ("configured={0} gcc={1} mingw={2}" -f $configured, $gccOk, $MinGwBin)
        exit 0
    }

    Write-KV "STATUS" "STOPPED pid=none count=0"
    Write-KV "EXE" ("path={0} exists={1} size={2} mtime={3}" -f `
        $exeInfo.Path, $exeInfo.Exists, $exeInfo.Size, $exeInfo.MTime)
    Write-KV "BUILD" ("configured={0} gcc={1} mingw={2}" -f $configured, $gccOk, $MinGwBin)

    $sum = Get-ProgressSummary
    if ($sum.Overview.Count -gt 0) {
        foreach ($l in $sum.Overview) { Write-KV "PROGRESS" $l }
    } else {
        Write-KV "PROGRESS" "(no overview rows parsed)"
    }
    if ($sum.Next.Count -gt 0) {
        $max = [Math]::Min(4, $sum.Next.Count - 1)
        foreach ($i in 0..$max) { Write-KV "NEXT" $sum.Next[$i] }
    }

    exit 1
}

function Start-Studio {
    $procs = Get-StudioProcesses
    if ($procs.Count -gt 0) {
        Write-KV "ACTION" ("start result=already_running pid={0}" -f $procs[0].Id)
        exit 0
    }

    if (-not (Test-Path -LiteralPath $Exe)) {
        Write-KV "ACTION" ("start result=exe_missing path={0}" -f $Exe)
        Write-KV "HINT" "run: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\studio.ps1 build"
        exit 1
    }

    $workDir = Split-Path -Parent $Exe
    $proc = Start-Process -FilePath $Exe -WorkingDirectory $workDir -PassThru
    Write-KV "ACTION" ("start result=launched pid={0} path={1}" -f $proc.Id, $Exe)

    $deadline = [DateTime]::UtcNow.AddSeconds($StartWaitSec)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
        $live = Get-StudioProcesses
        if ($live.Count -gt 0) { break }
        try {
            $proc.Refresh()
            if ($proc.HasExited) {
                Write-KV "ACTION" ("start result=exited_early pid={0} code={1}" -f $proc.Id, $proc.ExitCode)
                exit 1
            }
        } catch {}
    }

    $live = Get-StudioProcesses
    if ($live.Count -eq 0) {
        Write-KV "ACTION" "start result=timeout_no_process"
        exit 1
    }

    if ($WindowWaitSec -gt 0) {
        $wDeadline = [DateTime]::UtcNow.AddSeconds($WindowWaitSec)
        $hwnd = [IntPtr]::Zero
        $title = ""
        while ([DateTime]::UtcNow -lt $wDeadline) {
            $hwnd = Get-WindowHandle
            if ($hwnd -eq [IntPtr]::Zero) {
                # Fallback: process MainWindowHandle / title (DX11 shell can take >8s).
                try {
                    $liveNow = Get-StudioProcesses
                    if ($liveNow.Count -gt 0) {
                        $liveNow[0].Refresh()
                        if ($liveNow[0].MainWindowHandle -ne [IntPtr]::Zero -and
                            $liveNow[0].MainWindowTitle -eq $WindowTitle) {
                            $hwnd = $liveNow[0].MainWindowHandle
                            $title = $liveNow[0].MainWindowTitle
                            break
                        }
                    }
                } catch {}
            } else {
                $title = $WindowTitle
                break
            }
            Start-Sleep -Milliseconds 300
        }
        if ($hwnd -eq [IntPtr]::Zero) {
            Write-KV "ACTION" ("start result=running_no_window pid={0} title={1}" -f $live[0].Id, $WindowTitle)
            exit 0
        }
        Write-KV "ACTION" ("start result=running pid={0} hwnd={1} title={2}" -f $live[0].Id, $hwnd, $title)
        exit 0
    }

    Write-KV "ACTION" ("start result=running pid={0}" -f $live[0].Id)
    exit 0
}

function Stop-Studio {
    $procs = Get-StudioProcesses
    if ($procs.Count -eq 0) {
        Write-KV "ACTION" "stop result=already_stopped"
        exit 0
    }

    foreach ($p in $procs) {
        try {
            $null = $p.CloseMainWindow()
            Write-KV "ACTION" ("stop result=close_main_window pid={0}" -f $p.Id)
        } catch {
            Write-KV "ACTION" ("stop result=close_failed pid={0} err={1}" -f $p.Id, $_.Exception.Message)
        }
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($StopWaitSec)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if ((Get-StudioProcesses).Count -eq 0) {
            Write-KV "ACTION" "stop result=stopped"
            exit 0
        }
    }

    $live = Get-StudioProcesses
    foreach ($p in $live) {
        Write-KV "ACTION" ("stop result=force_kill pid={0}" -f $p.Id)
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 400
    $live2 = Get-StudioProcesses
    if ($live2.Count -eq 0) {
        Write-KV "ACTION" "stop result=stopped_after_force"
        exit 0
    }
    Write-KV "ACTION" ("stop result=still_running count={0}" -f $live2.Count)
    exit 1
}

function Restart-Studio {
    Write-KV "ACTION" "restart begin"
    $procs = Get-StudioProcesses
    if ($procs.Count -gt 0) {
        foreach ($p in $procs) {
            try { $null = $p.CloseMainWindow() } catch {}
        }
        $deadline = [DateTime]::UtcNow.AddSeconds($StopWaitSec)
        while ([DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if ((Get-StudioProcesses).Count -eq 0) { break }
        }
        foreach ($p in (Get-StudioProcesses)) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Milliseconds 400
    }

    if (-not (Test-Path -LiteralPath $Exe)) {
        Write-KV "ACTION" ("restart result=exe_missing path={0}" -f $Exe)
        exit 1
    }

    $workDir = Split-Path -Parent $Exe
    $proc = Start-Process -FilePath $Exe -WorkingDirectory $workDir -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds($StartWaitSec)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if ((Get-StudioProcesses).Count -gt 0) { break }
        try {
            $proc.Refresh()
            if ($proc.HasExited) {
                Write-KV "ACTION" ("restart result=exited_early pid={0} code={1}" -f $proc.Id, $proc.ExitCode)
                exit 1
            }
        } catch {}
    }

    $live = Get-StudioProcesses
    if ($live.Count -eq 0) {
        Write-KV "ACTION" "restart result=timeout_no_process"
        exit 1
    }
    Write-KV "ACTION" ("restart result=running pid={0}" -f $live[0].Id)
    exit 0
}

function Show-Progress {
    $sum = Get-ProgressSummary
    Write-KV "PROGRESS_PATH" $ProgressPath
    foreach ($l in $sum.Overview) { Write-KV "PROGRESS" $l }
    foreach ($l in $sum.Next) { Write-KV "NEXT" $l }

    Write-KV "NOVEL_PATH" $NovelProgressPath
    foreach ($l in (Get-NovelProgressSummary)) { Write-KV "NOVEL" $l }

    if ($sum.Overview.Count -eq 0 -and $sum.Next.Count -eq 0) {
        exit 1
    }
    exit 0
}

function Build-Studio {
    if (-not (Test-Path -LiteralPath $Gcc)) {
        Write-KV "ACTION" ("build result=gcc_missing path={0}" -f $Gcc)
        exit 1
    }
    if (-not (Test-Path -LiteralPath $Make)) {
        Write-KV "ACTION" ("build result=make_missing path={0}" -f $Make)
        exit 1
    }

    $env:PATH = "{0};{1}" -f $MinGwBin, $env:PATH

    $configured = Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt")
    if (-not $configured) {
        Write-KV "ACTION" "build step=configure"
        & cmake -S $Root -B $BuildDir -G "MinGW Makefiles" `
            ("-DCMAKE_C_COMPILER={0}" -f $Gcc) `
            ("-DCMAKE_CXX_COMPILER={0}" -f $Gxx) `
            ("-DCMAKE_MAKE_PROGRAM={0}" -f $Make) `
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
        if ($LASTEXITCODE -ne 0) {
            Write-KV "ACTION" ("build result=configure_failed code={0}" -f $LASTEXITCODE)
            exit 1
        }
    } else {
        Write-KV "ACTION" "build step=configure_skipped cache=exists"
    }

    Write-KV "ACTION" ("build step=compile jobs={0}" -f $Jobs)
    & cmake --build $BuildDir -j $Jobs
    if ($LASTEXITCODE -ne 0) {
        Write-KV "ACTION" ("build result=compile_failed code={0}" -f $LASTEXITCODE)
        exit 1
    }

    $exeInfo = Get-ExeInfo
    if (-not $exeInfo.Exists) {
        Write-KV "ACTION" ("build result=exe_missing path={0}" -f $Exe)
        exit 1
    }
    Write-KV "ACTION" ("build result=ok path={0} size={1} mtime={2}" -f `
        $exeInfo.Path, $exeInfo.Size, $exeInfo.MTime)
    exit 0
}

function Show-All {
    $procs = Get-StudioProcesses
    $exeInfo = Get-ExeInfo
    $configured = Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt")
    $gccOk = Test-Path -LiteralPath $Gcc

    if ($procs.Count -gt 0) {
        $p = $procs[0]
        $responding = $true
        try { $responding = $p.Responding } catch { $responding = $true }
        Write-KV "STATUS" ("RUNNING pid={0} responding={1} count={2}" -f $p.Id, $responding, $procs.Count)
    } else {
        Write-KV "STATUS" "STOPPED pid=none count=0"
    }
    Write-KV "EXE" ("path={0} exists={1} size={2} mtime={3}" -f `
        $exeInfo.Path, $exeInfo.Exists, $exeInfo.Size, $exeInfo.MTime)
    Write-KV "BUILD" ("configured={0} gcc={1}" -f $configured, $gccOk)

    $sum = Get-ProgressSummary
    foreach ($l in $sum.Overview) { Write-KV "PROGRESS" $l }
    foreach ($l in $sum.Next) { Write-KV "NEXT" $l }
    foreach ($l in (Get-NovelProgressSummary)) { Write-KV "NOVEL" $l }
    exit 0
}

function Show-Help {
    Write-Output "ShineTV studio.ps1 -- process + progress control"
    Write-Output "Root: $Root"
    Write-Output "Exe:  $Exe"
    Write-Output ""
    Write-Output "Commands:"
    Write-Output "  status    RUNNING/STOPPED + exe/build (exit 0=running, 1=stopped)"
    Write-Output "  start     launch studio if not running"
    Write-Output "  stop      graceful close, then force"
    Write-Output "  restart   stop + start"
    Write-Output "  progress  parse Plan/PROGRESS.md + novel PROGRESS"
    Write-Output "  build     cmake configure (if needed) + build -j $Jobs"
    Write-Output "  all       status + progress"
    Write-Output "  help      this text"
    Write-Output ""
    Write-Output "Example:"
    Write-Output "  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\studio.ps1 restart"
    exit 0
}

Write-KV "ROOT" $Root
Write-KV "CMD" $Cmd
Write-KV "EXE" $Exe

switch ($Cmd) {
    "status"   { Show-Status }
    "start"    { Start-Studio }
    "stop"     { Stop-Studio }
    "restart"  { Restart-Studio }
    "progress" { Show-Progress }
    "build"    { Build-Studio }
    "all"      { Show-All }
    "help"     { Show-Help }
    default    { Show-Help }
}
