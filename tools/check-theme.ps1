$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$themeFiles = Get-ChildItem (Join-Path $root 'src/ui/kit/theme/Themes') -Filter '*.json' -File
if ($themeFiles.Count -lt 4) { Write-Output 'check-theme: expected four theme files'; exit 1 }
foreach ($file in $themeFiles) { Get-Content -Raw -LiteralPath $file.FullName | ConvertFrom-Json | Out-Null }
Write-Output 'check-theme: PASS (four themes parse)'
