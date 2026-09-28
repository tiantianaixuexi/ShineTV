$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$files = Get-ChildItem "$root/src" -Recurse -Include *.h,*.hpp,*.cpp -File
$bad = @($files | Select-String -Pattern ([char]0xFFFD))
if ($bad.Count -gt 0) { $bad | ForEach-Object { Write-Output $_.ToString() }; exit 1 }
Write-Output 'check-i18n: PASS (no replacement glyphs)'
