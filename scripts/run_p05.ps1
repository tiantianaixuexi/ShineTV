# run_p05.ps1 -- run P05 acceptance scenes offscreen
#
# ASCII-only on disk and in literals: Windows PowerShell 5.1 reads .ps1 as ANSI
# when there is no BOM, and non-ASCII literals get mangled. Scene/report paths
# are ASCII, so nothing here needs to be localized.
#
# Usage:  pwsh/powershell -File scripts/run_p05.ps1 -Scenes S1,S2
#         powershell -File scripts/run_p05.ps1            # all scenes
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build/ShineTVStudio.exe'
if (-not (Test-Path $exe)) { Write-Error "missing exe: $exe" }

$all = @('S1','S2','S3','S4','S5','S6','S7','S8')
$scenes = if ($args.Count -ge 1 -and $args[0]) { $args[0].Split(',') } else { $all }

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

  $p = Start-Process -FilePath $exe -PassThru -NoNewWindow -Wait `
         -RedirectStandardOutput (Join-Path $outDir "p05_$s.out") `
         -RedirectStandardError  (Join-Path $outDir "p05_$s.err")
  $code = $p.ExitCode
  $verdict = 'MISSING'
  if (Test-Path $report) {
    $verdict = (Select-String -Path $report -Pattern '\[P05-\w+\] overall: (\w+)' |
                Select-Object -First 1).Matches.Groups[1].Value
    if (-not $verdict) { $verdict = 'NO-VERDICT' }
  }
  $results += [pscustomobject]@{ Scene = $s; Exit = $code; Verdict = $verdict }
  Write-Host ("{0,-4} exit={1,-4} {2}" -f $s, $code, $verdict)
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
