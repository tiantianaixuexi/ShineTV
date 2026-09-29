# run_reviews.ps1 -- run every P0x visual review offscreen and check the verdict
#
# ASCII-only (PowerShell 5.1 reads .ps1 as ANSI without a BOM).
# Usage: powershell -File scripts/run_reviews.ps1            # all
#        powershell -File scripts/run_reviews.ps1 P05 P09
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build/ShineTVStudio.exe'
if (-not (Test-Path $exe)) { Write-Error "missing exe: $exe" }

$all = @('P03', 'P04', 'P05', 'P06', 'P07', 'P08', 'P09', 'P10')
$want = if ($args.Count -ge 1) { $args } else { $all }
$unknown = $want | Where-Object { $all -notcontains $_ }
if ($unknown) { Write-Error ("unknown review(s): " + ($unknown -join ', ')) }

$root = Join-Path $repo 'build/reviews'
if (Test-Path $root) { Remove-Item -Recurse -Force $root }
New-Item -ItemType Directory -Force -Path $root | Out-Null

$env:QT_QPA_PLATFORM = 'offscreen'

$results = @()
foreach ($r in $want) {
  # Clear the whole family so a stale var from a previous review cannot leak in.
  foreach ($n in $all + @('P02')) {
    [Environment]::SetEnvironmentVariable("SHINE_${n}_REVIEW", $null, 'Process')
  }
  $dir = Join-Path $root $r
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  [Environment]::SetEnvironmentVariable("SHINE_${r}_REVIEW", $dir, 'Process')

  $out = Join-Path $dir 'run.out'
  $err = Join-Path $dir 'run.err'
  $stamp = Join-Path $dir 'timeout'

  # Watchdog for the hard timeout; see run_p05.ps1 for why the subject stays in
  # the foreground (that is the only way $LASTEXITCODE populates here).
  $watchdog = Start-Job -ScriptBlock {
    param($marker, $limit)
    Start-Sleep -Seconds $limit
    if (Get-Process ShineTVStudio -ErrorAction SilentlyContinue) {
      New-Item -ItemType File -Path $marker -Force | Out-Null
      Get-Process ShineTVStudio -ErrorAction SilentlyContinue | Stop-Process -Force
    }
  } -ArgumentList $stamp, 300

  cmd /c ('""{0}" 1> ""{1}"" 2> ""{2}""' -f $exe, $out, $err)
  $code = $LASTEXITCODE
  Stop-Job $watchdog -ErrorAction SilentlyContinue
  if (Test-Path $stamp) {
    Write-Host "$r TIMEOUT after 300s"
    $code = 'TIMEOUT'
  }

  $report = Join-Path $dir 'shots-manifest.txt'
  $verdict = 'NO-REPORT'
  $shots = 0
  if (Test-Path $report) {
    $text = Get-Content $report -Raw
    if ($text -match 'overall=(\w+)') { $verdict = $Matches[1] }
    $shots = @(Get-ChildItem $dir -Filter *.png -ErrorAction SilentlyContinue).Count
  }
  # The point of this round: a review that says PASS must exit 0, and one that
  # says FAIL must exit non-zero. Either disagreement is a false green.
  $expected = if ($verdict -eq 'PASS') { 0 } else { 1 }
  $agree = ($code -eq $expected)
  $results += [pscustomobject]@{
    Review = $r; Shots = $shots; Verdict = $verdict; Exit = $code; Agree = $agree
  }
  Write-Host ("{0} shots={1,-4} overall={2,-6} exit={3,-8} agree={4}" -f $r, $shots, $verdict, $code, $agree)
}

Write-Host ''
$results | Format-Table -AutoSize
$bad = $results | Where-Object { -not $_.Agree -or $_.Verdict -ne 'PASS' }
if ($bad) {
  Write-Host "NOT ALL GREEN:" ($bad | ForEach-Object {
    "$($_.Review)(overall=$($_.Verdict),exit=$($_.Exit))" }) -Separator ', '
} else {
  Write-Host 'ALL GREEN'
}
Write-Host "reports: $root"
