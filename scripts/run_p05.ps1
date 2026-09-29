# run_p05.ps1 -- run P05 acceptance scenes offscreen
#
# ASCII-only on disk and in literals: Windows PowerShell 5.1 reads .ps1 as ANSI
# when there is no BOM, and non-ASCII literals get mangled. Scene/report paths
# are ASCII, so nothing here needs to be localized.
#
# Usage:  powershell -File scripts/run_p05.ps1                    # all scenes
#         powershell -File scripts/run_p05.ps1 S7,S8             # positional
#         powershell -File scripts/run_p05.ps1 -Scenes S7,S8     # named
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build/ShineTVStudio.exe'
if (-not (Test-Path $exe)) { Write-Error "missing exe: $exe" }

$all = @('S1','S2','S3','S4','S5','S6','S7','S8')
# Accept both the positional and the -Scenes form. With -File, PowerShell hands
# every token to $args, so `-Scenes S7,S8` arrives as $args[0]='-Scenes',
# $args[1]='S7,S8' -- reading $args[0] alone would run a bogus scene named
# "-Scenes", which matches no check: the app enters its normal main loop and
# never exits, so the harness hangs instead of reporting.
$spec = $null
for ($i = 0; $i -lt $args.Count; $i++) {
  $a = $args[$i]
  if ($a -eq '-Scenes' -or $a -eq '--scenes') {
    if ($i + 1 -ge $args.Count) { Write-Error '-Scenes needs a value' }
    $spec = $args[$i + 1]; $i++
  } elseif (-not $a.StartsWith('-')) {
    $spec = $a
  }
}
$scenes = if ($spec) { $spec.Split(',') } else { $all }
$unknown = $scenes | Where-Object { $all -notcontains $_ }
if ($unknown) { Write-Error ("unknown scene(s): " + ($unknown -join ', ')) }

$outDir = Join-Path $repo 'build/p05'
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$env:QT_QPA_PLATFORM = 'offscreen'

$results = @()
foreach ($s in $scenes) {
  $report = Join-Path $outDir "p05_$s.txt"
  # Clear the whole family so a stale env var from a previous scene cannot leak in.
  foreach ($n in $all) {
    [Environment]::SetEnvironmentVariable("SHINE_P05_$n", $null, 'Process')
  }
  if ($s -eq 'S7' -or $s -eq 'S8') {
    [Environment]::SetEnvironmentVariable("SHINE_P05_$s", $outDir, 'Process')
    [Environment]::SetEnvironmentVariable("SHINE_P05_${s}_OUT", $report, 'Process')
  } else {
    [Environment]::SetEnvironmentVariable("SHINE_P05_$s", $report, 'Process')
  }

  # Taking the exit code took three wrong turns on this machine; the conclusions
  # are recorded here so nobody repeats them:
  #   1. `Start-Process -PassThru` hands back a Process object whose ExitCode is
  #      ALWAYS EMPTY here (same for running the exe directly, and for wrapping it
  #      in cmd /c). That is why the Exit column used to be blank decoration.
  #   2. Only a FOREGROUND `cmd /c` populates $LASTEXITCODE (verified working).
  #   3. But a foreground call has no timeout. So the timeout is a **watchdog
  #      process** instead of pushing the subject into Start-Job -- pushing the
  #      subject into a Job was measured to lose its output (NO-EXITCODE).
  # Exit code and report verdict must agree; a PASS that exits non-zero is itself
  # a false green, so it is surfaced as PASS-BUT-EXIT instead of being dropped.
  $out = Join-Path $outDir "p05_$s.out"
  $err = Join-Path $outDir "p05_$s.err"
  $stamp = Join-Path $outDir "p05_$s.timeout"

  # Watchdog: exits on its own if not triggered; writes a marker when it fires.
  $watchdog = Start-Job -ScriptBlock {
    param($marker, $limit)
    Start-Sleep -Seconds $limit
    if (Get-Process ShineTVStudio -ErrorAction SilentlyContinue) {
      New-Item -ItemType File -Path $marker -Force | Out-Null
      Get-Process ShineTVStudio -ErrorAction SilentlyContinue | Stop-Process -Force
    }
  } -ArgumentList $stamp, 120

  $cmdline = '""{0}" 1> ""{1}"" 2> ""{2}""' -f $exe, $out, $err
  cmd /c $cmdline
  $code = $LASTEXITCODE
  Stop-Job $watchdog -ErrorAction SilentlyContinue
  if (Test-Path $stamp) {
    Write-Host "TIMEOUT after 120s: $s"
    $code = 'TIMEOUT'
  }

  $verdict = 'MISSING'
  if (Test-Path $report) {
    $verdict = (Select-String -Path $report -Pattern '\[P05-\w+\] overall: (\w+)' |
                Select-Object -First 1).Matches.Groups[1].Value
    if (-not $verdict) { $verdict = 'NO-VERDICT' }
  }
  if ($verdict -eq 'PASS' -and $code -ne 0) { $verdict = "PASS-BUT-EXIT=$code" }
  $results += [pscustomobject]@{ Scene = $s; Exit = $code; Verdict = $verdict }
  Write-Host ("{0,-4} exit={1,-8} {2}" -f $s, $code, $verdict)
}

Write-Host ''
$results | Format-Table -AutoSize
$bad = $results | Where-Object { $_.Verdict -ne 'PASS' }
if ($bad) {
  Write-Host "NOT ALL PASS:" ($bad | ForEach-Object { "$($_.Scene)=$($_.Verdict)" }) -Separator ', '
} else {
  Write-Host 'ALL PASS'
}
Write-Host "reports: $outDir"
