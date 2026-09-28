param([int]$Runs = 10)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build/ShineTVStudio.exe'
if (-not (Test-Path $exe)) { Write-Output 'stability: build/ShineTVStudio.exe missing'; exit 1 }
for ($i = 1; $i -le $Runs; $i++) {
  $env:SHINE_P06_S1 = Join-Path $env:TEMP "shinetv-stability-$i.txt"
  $process = Start-Process -FilePath $exe -PassThru -Wait
  if ($process.ExitCode -ne 0) { Write-Output "stability: run $i failed ($($process.ExitCode))"; exit 1 }
}
Remove-Item Env:SHINE_P06_S1 -ErrorAction SilentlyContinue
Write-Output "stability: PASS ($Runs clean start/stop runs)"
