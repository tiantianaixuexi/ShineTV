# scripts/comfy.ps1 -- ComfyUI start/stop/restart/status/progress (pure ASCII for PS 5.1).
#
# Usage (from anywhere):
#   powershell -NoProfile -ExecutionPolicy Bypass -File E:\c++\ShineTV\scripts\comfy.ps1 status
#   powershell -NoProfile -ExecutionPolicy Bypass -File E:\c++\ShineTV\scripts\comfy.ps1 start
#   powershell -NoProfile -ExecutionPolicy Bypass -File E:\c++\ShineTV\scripts\comfy.ps1 stop
#   powershell -NoProfile -ExecutionPolicy Bypass -File E:\c++\ShineTV\scripts\comfy.ps1 restart
#   powershell -NoProfile -ExecutionPolicy Bypass -File E:\c++\ShineTV\scripts\comfy.ps1 progress
#
# Exit codes:
#   status: 0 = ACTIVE (port listening), 1 = INACTIVE
#   start/restart: 0 = ACTIVE after call, 1 = failed
#   stop: 0 = INACTIVE after call, 1 = still listening
#   progress: 0 = ok, 1 = inactive or HTTP failed
#
# Output tokens: COMFY STATUS|ACTION|QUEUE|HISTORY|...

param(
    [Parameter(Position = 0)]
    [ValidateSet("start", "stop", "restart", "status", "progress", "help")]
    [string]$Cmd = "status",
    [int]$Port = 8188,
    [int]$StartWaitSec = 90,
    [int]$StopWaitSec = 8,
    [string]$Python = "F:\AI\ComfyUI-aki-v3\python\python.exe",
    [string]$ComfyDir = "F:\AI\ComfyUI-aki-v3\ComfyUI"
)

$ErrorActionPreference = "Continue"
$MainPy = Join-Path $ComfyDir "main.py"
$BaseUrl = "http://127.0.0.1:$Port"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$LogDir = Join-Path (Split-Path -Parent $ScriptDir) "build"
$OutLog = Join-Path $LogDir "comfy.out.log"
$ErrLog = Join-Path $LogDir "comfy.err.log"

function Out([string]$s) { Write-Output $s }

function Test-Listening {
    return [bool](Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
}

function Get-PortOwner {
    $c = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $c) { return $null }
    return Get-Process -Id $c.OwningProcess -ErrorAction SilentlyContinue
}

function Get-ComfyProcesses {
    $byCmd = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -match 'python' -and $_.CommandLine -match 'ComfyUI' -and $_.CommandLine -match 'main\.py'
    } | ForEach-Object { Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue })
    $owner = Get-PortOwner
    $all = @()
    foreach ($p in $byCmd) { if ($p) { $all += $p } }
    if ($owner -and -not ($all | Where-Object { $_.Id -eq $owner.Id })) { $all += $owner }
    # unique by Id
    $seen = @{}
    $uniq = @()
    foreach ($p in $all) {
        if ($p -and -not $seen.ContainsKey($p.Id)) { $seen[$p.Id] = $true; $uniq += $p }
    }
    return $uniq
}

function Invoke-ComfyGet([string]$Path, [int]$TimeoutSec = 5) {
    $uri = "$BaseUrl$Path"
    try {
        return Invoke-WebRequest -Uri $uri -UseBasicParsing -TimeoutSec $TimeoutSec
    } catch {
        return $null
    }
}

function Show-Status {
    $listening = Test-Listening
    $owner = Get-PortOwner
    $procs = Get-ComfyProcesses
    $http = $null
    if ($listening) { $http = Invoke-ComfyGet "/queue" 3 }

    if ($listening) {
        $pidTxt = if ($owner) { $owner.Id } else { 0 }
        $resp = if ($http) { "ok" } else { "port_only_http_fail" }
        Out ("COMFY STATUS ACTIVE port={0} pid={1} http={2} url={3}" -f $Port, $pidTxt, $resp, $BaseUrl)
        if ($http) {
            try {
                $j = $http.Content | ConvertFrom-Json
                $run = @($j.queue_running).Count
                $pend = @($j.queue_pending).Count
                Out ("COMFY QUEUE running={0} pending={1}" -f $run, $pend)
            } catch {}
        }
        exit 0
    }

    Out ("COMFY STATUS INACTIVE port={0} pid=none procs={1} url={2}" -f $Port, $procs.Count, $BaseUrl)
    Out ("COMFY PATH python={0} main={1} exists={2}" -f $Python, $MainPy, (Test-Path $MainPy))
    exit 1
}

function Start-Comfy {
    if (Test-Listening) {
        $owner = Get-PortOwner
        Out ("COMFY ACTION start result=already_running pid={0}" -f $(if ($owner) { $owner.Id } else { 0 }))
        exit 0
    }
    if (-not (Test-Path $Python)) {
        Out ("COMFY ACTION start result=python_missing path={0}" -f $Python)
        exit 1
    }
    if (-not (Test-Path $MainPy)) {
        Out ("COMFY ACTION start result=main_missing path={0}" -f $MainPy)
        exit 1
    }
    if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Force -Path $LogDir | Out-Null }

    # --disable-auto-launch: no browser. Redirect is required on this machine (see HANDOFF).
    $argList = @("main.py", "--port", "$Port", "--disable-auto-launch")
    $proc = Start-Process -FilePath $Python -ArgumentList $argList -WorkingDirectory $ComfyDir `
        -RedirectStandardOutput $OutLog -RedirectStandardError $ErrLog -PassThru -WindowStyle Hidden
    Out ("COMFY ACTION start result=launched pid={0} port={1}" -f $proc.Id, $Port)
    Out ("COMFY LOG out={0}" -f $OutLog)
    Out ("COMFY LOG err={0}" -f $ErrLog)

    $deadline = [DateTime]::UtcNow.AddSeconds($StartWaitSec)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Seconds 1
        if (Test-Listening) {
            Start-Sleep -Milliseconds 500
            $owner = Get-PortOwner
            $http = Invoke-ComfyGet "/queue" 5
            $pidTxt = if ($owner) { $owner.Id } else { $proc.Id }
            if ($http) {
                Out ("COMFY ACTION start result=active pid={0} http=ok url={1}" -f $pidTxt, $BaseUrl)
                exit 0
            }
            Out ("COMFY ACTION start result=active_port pid={0} http=fail url={1}" -f $pidTxt, $BaseUrl)
            exit 0
        }
        try {
            $proc.Refresh()
            if ($proc.HasExited) {
                Out ("COMFY ACTION start result=exited_early pid={0} code={1}" -f $proc.Id, $proc.ExitCode)
                if (Test-Path $ErrLog) {
                    $tail = Get-Content $ErrLog -Tail 15 -ErrorAction SilentlyContinue
                    foreach ($l in $tail) { Out ("COMFY ERR {0}" -f $l) }
                }
                exit 1
            }
        } catch {}
    }
    Out ("COMFY ACTION start result=timeout port={0} waitSec={1}" -f $Port, $StartWaitSec)
    if (Test-Path $ErrLog) {
        $tail = Get-Content $ErrLog -Tail 15 -ErrorAction SilentlyContinue
        foreach ($l in $tail) { Out ("COMFY ERR {0}" -f $l) }
    }
    exit 1
}

function Stop-Comfy {
    if (-not (Test-Listening) -and (Get-ComfyProcesses).Count -eq 0) {
        Out "COMFY ACTION stop result=already_stopped"
        exit 0
    }

    foreach ($p in (Get-ComfyProcesses)) {
        Out ("COMFY ACTION stop result=kill pid={0}" -f $p.Id)
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    }
    # Port owner may be a different python wrapper
    $owner = Get-PortOwner
    if ($owner) {
        Out ("COMFY ACTION stop result=kill_port_owner pid={0}" -f $owner.Id)
        Stop-Process -Id $owner.Id -Force -ErrorAction SilentlyContinue
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($StopWaitSec)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (-not (Test-Listening)) {
            Out "COMFY ACTION stop result=stopped"
            exit 0
        }
    }
    Out ("COMFY ACTION stop result=still_listening port={0}" -f $Port)
    exit 1
}

function Restart-Comfy {
    Out "COMFY ACTION restart begin"
    if (Test-Listening -or (Get-ComfyProcesses).Count -gt 0) {
        foreach ($p in (Get-ComfyProcesses)) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
        $owner = Get-PortOwner
        if ($owner) { Stop-Process -Id $owner.Id -Force -ErrorAction SilentlyContinue }
        $deadline = [DateTime]::UtcNow.AddSeconds($StopWaitSec)
        while ([DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (-not (Test-Listening)) { break }
        }
    }
    # Re-enter start logic without exiting this function via call operator pattern:
    if (Test-Listening) {
        Out "COMFY ACTION restart result=stop_failed"
        exit 1
    }
    if (-not (Test-Path $Python) -or -not (Test-Path $MainPy)) {
        Out "COMFY ACTION restart result=path_missing"
        exit 1
    }
    if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Force -Path $LogDir | Out-Null }
    $argList = @("main.py", "--port", "$Port", "--disable-auto-launch")
    $proc = Start-Process -FilePath $Python -ArgumentList $argList -WorkingDirectory $ComfyDir `
        -RedirectStandardOutput $OutLog -RedirectStandardError $ErrLog -PassThru -WindowStyle Hidden
    Out ("COMFY ACTION restart result=launched pid={0}" -f $proc.Id)
    $deadline = [DateTime]::UtcNow.AddSeconds($StartWaitSec)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Seconds 1
        if (Test-Listening) {
            Start-Sleep -Milliseconds 400
            $http = Invoke-ComfyGet "/queue" 5
            Out ("COMFY ACTION restart result=active http={0} url={1}" -f $(if ($http) { "ok" } else { "fail" }), $BaseUrl)
            exit 0
        }
        try {
            $proc.Refresh()
            if ($proc.HasExited) {
                Out ("COMFY ACTION restart result=exited_early code={0}" -f $proc.ExitCode)
                if (Test-Path $ErrLog) {
                    foreach ($l in (Get-Content $ErrLog -Tail 12 -ErrorAction SilentlyContinue)) {
                        Out ("COMFY ERR {0}" -f $l)
                    }
                }
                exit 1
            }
        } catch {}
    }
    Out "COMFY ACTION restart result=timeout"
    exit 1
}

function Show-Progress {
    if (-not (Test-Listening)) {
        Out ("COMFY STATUS INACTIVE port={0}" -f $Port)
        Out "COMFY ACTION progress result=inactive"
        exit 1
    }

    $q = Invoke-ComfyGet "/queue" 8
    if ($q) {
        try {
            $j = $q.Content | ConvertFrom-Json
            $run = @($j.queue_running)
            $pend = @($j.queue_pending)
            Out ("COMFY QUEUE running={0} pending={1}" -f $run.Count, $pend.Count)
            if ($run.Count -gt 0) {
                $item = $run[0]
                # queue_running: [number, prompt_id, prompt_dict]
                $promptId = ""
                try { $promptId = $item[1] } catch {}
                Out ("COMFY RUNNING prompt_id={0}" -f $promptId)
            }
        } catch {
            Out "COMFY QUEUE parse_fail"
        }
    } else {
        Out "COMFY QUEUE http_fail"
    }

    $h = Invoke-ComfyGet "/history?max_items=5" 8
    if ($h) {
        try {
            $jh = $h.Content | ConvertFrom-Json
            $props = $jh.PSObject.Properties
            $n = @($props).Count
            Out ("COMFY HISTORY items={0}" -f $n)
            $i = 0
            foreach ($p in $props) {
                if ($i -ge 5) { break }
                $status = ""
                try { $status = ($p.Value.status.status_str) } catch {}
                Out ("COMFY HIST id={0} status={1}" -f $p.Name, $status)
                $i++
            }
        } catch {
            Out "COMFY HISTORY parse_fail"
        }
    } else {
        Out "COMFY HISTORY http_fail"
    }

    $s = Invoke-ComfyGet "/system_stats" 5
    if ($s) {
        try {
            $js = $s.Content | ConvertFrom-Json
            $dev = ""
            try { $dev = $js.devices[0].name } catch {}
            Out ("COMFY SYSTEM device={0}" -f $dev)
        } catch {}
    }

    # Optional: last error lines from our start logs
    if (Test-Path $ErrLog) {
        $item = Get-Item $ErrLog
        Out ("COMFY LOGERR mtime={0} size={1}" -f $item.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"), $item.Length)
    }
    exit 0
}

function Show-Help {
    Out "ComfyUI comfy.ps1"
    Out "  status | start | stop | restart | progress | help"
    Out ("  Port={0} Python={1} Dir={2}" -f $Port, $Python, $ComfyDir)
    Out ("  Logs: {0} / {1}" -f $OutLog, $ErrLog)
    exit 0
}

Out ("COMFY CMD {0} port={1}" -f $Cmd, $Port)
switch ($Cmd) {
    "status"   { Show-Status }
    "start"    { Start-Comfy }
    "stop"     { Stop-Comfy }
    "restart"  { Restart-Comfy }
    "progress" { Show-Progress }
    "help"     { Show-Help }
    default    { Show-Help }
}
