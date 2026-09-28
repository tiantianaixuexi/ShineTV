$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$files = Get-ChildItem "$root/src" -Recurse -Include *.h,*.hpp,*.cpp -File
$bad = @($files | Select-String -Pattern '(?i)(setStyleSheet|QColor)\s*\(?[^\r\n]*#[0-9A-F]{6,8}')
if ($bad.Count -gt 0) { $bad | ForEach-Object { Write-Output $_.ToString() }; exit 1 }
$themeFiles = Get-ChildItem "$root/src/ui/kit/theme/Themes" -Filter *.json -File
if ($themeFiles.Count -lt 4) { Write-Output 'check-colors: expected four theme files'; exit 1 }
Write-Output "check-colors: PASS ($($themeFiles.Count) themes, 0 hard-coded style colors)"
