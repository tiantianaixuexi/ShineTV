$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$themeFiles = Get-ChildItem (Join-Path $root 'src/ui/imgui/theme/Themes') -Filter '*.json' -File
if ($themeFiles.Count -lt 5) { Write-Output 'check-theme: expected five theme files'; exit 1 }
foreach ($file in $themeFiles) { Get-Content -Raw -LiteralPath $file.FullName | ConvertFrom-Json | Out-Null }
Write-Output "check-theme: PASS ($($themeFiles.Count) themes parse)"
