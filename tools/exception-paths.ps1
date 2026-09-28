$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build/ShineTVStudio.exe'
$cases = @(
  @{ Name='comfy-offline'; Env='SHINE_P09_S7'; Out='p09_exception_comfy.txt' },
  @{ Name='prompt-versions'; Env='SHINE_P07_S12'; Out='p07_exception_prompt.txt' },
  @{ Name='startup-smoke'; Env='SHINE_P06_S1'; Out='p06_exception_startup.txt' }
)
foreach ($case in $cases) {
  $path = Join-Path $env:TEMP $case.Out
  [Environment]::SetEnvironmentVariable($case.Env, $path, 'Process')
  $p = Start-Process -FilePath $exe -PassThru -Wait
  [Environment]::SetEnvironmentVariable($case.Env, $null, 'Process')
  if ($p.ExitCode -ne 0) { Write-Output "exception-paths: $($case.Name) failed"; exit 1 }
}
Write-Output 'exception-paths: PASS (Comfy offline / prompt version / startup smoke)'
