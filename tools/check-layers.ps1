# check-layers.ps1 -- ShineTV Studio layering gate
#
# Qt -> Dear ImGui refactor (see refactor/phases.md P0.3). Three rules:
#
# Rule 1 [qt-free]: no Qt header may appear outside the legacy Qt front end.
#         Refactor target: src/ is entirely Qt-free. During coexistence the
#         legacy front-end dirs (src/ui/app, kit, layout, pages, qml, verify)
#         are still Qt; P7 deletes them and this exemption disappears with
#         them. Everything else -- shine_core AND the new src/ui/imgui tree --
#         is already covered, so the gate never blocks its own new code.
#
# Rule 2: (removed) the old "no imgui/ImVec/ImDraw anywhere in src/" ban.
#         ImGui is now the front end; imgui.h / ImVec4 / ImDrawList are
#         ordinary headers here. What is still banned is the reverse leak:
#         shine_core must not include ImGui headers (business layer stays
#         host-agnostic and free of any UI dependency).
#
# Rule 3 [hardcoded-color]: colors come from the theme token table, never
#         from literals. Two shapes, one per front end:
#           legacy Qt tree  -- setStyleSheet("...#RRGGBB") / QColor("#...")
#           ImGui tree      -- ImVec4(0.1f, ...) / ImColor(0x...) / 0xRRGGBB
#         A line may opt out with a trailing `theme-ok` marker, which is
#         how theme/Tokens.* legitimately hold the source-of-truth values.
#
# Scope: code under src/ only.
# Usage: pwsh -File tools/check-layers.ps1
# Exit code 0 = pass, 1 = violations found.
# Script is pure ASCII on disk.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Directories that are allowed to be Qt (legacy front end, deleted in P7).
$legacyQtDirs = @(
  'src/ui/app', 'src/ui/kit', 'src/ui/layout',
  'src/ui/pages', 'src/ui/qml', 'src/ui/verify'
)

# Directories that must stay free of ANY UI header (business layer).
$coreDirs = @(
  'src/core', 'src/util', 'src/net', 'src/db', 'src/comfy',
  'src/media', 'src/flow', 'src/novel', 'src/visual', 'src/paint',
  'src/mcp', 'src/llm', 'src/project', 'src/pipeline', 'src/gpu'
)

# Case-sensitive on purpose: <Q... must be a Qt header, not <queue> / <QtMath-like>.
# This mirrors the original gate's -cmatch behaviour.
$qtIncludeRe = [regex]'#include\s*[<"]Q[A-Za-z]|^\s*Q_OBJECT\b'
$imguiLeakRe = [regex]'(?i)#include\s*[<"](imgui|ImDraw|ImGui)[>"]'
$qtColorRe = [regex]'(?i)(setStyleSheet|QColor)\s*\(?[^)]*#[0-9A-F]{6,8}'
# ImVec4(0.1f, ...) / ImVec4(0, 0, 0, 1) / ImColor(0x35D0B4) / 0xRRGGBB literals.
$imguiColorRe = [regex]'(?i)ImVec4\s*\(\s*[0-9]|ImColor\s*\(\s*[0-9]|\b0x[0-9A-F]{8}\b'

$violations = 0

$codeFiles = Get-ChildItem -LiteralPath (Join-Path $root 'src') -Recurse -File |
  Where-Object { $_.Extension -in '.h', '.hpp', '.cpp', '.cc' }

foreach ($f in $codeFiles) {
  $rel = $f.FullName.Substring($root.Length + 1) -replace '\\', '/'
  $inLegacyQt = $false
  foreach ($d in $legacyQtDirs) {
    if ($rel.StartsWith($d)) { $inLegacyQt = $true; break }
  }
  $inCore = $false
  foreach ($d in $coreDirs) {
    if ($rel.StartsWith($d)) { $inCore = $true; break }
  }
  $isQtUtil = $rel -like 'src/util/Qt*'

  $lines = [System.IO.File]::ReadAllLines($f.FullName)
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    $optOut = $line -match 'theme-ok'
    if (-not $inLegacyQt -and -not $isQtUtil -and $qtIncludeRe.IsMatch($line)) {
      Write-Output ('VIOLATION [qt-include] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
      $violations++
    }
    if ($inCore -and $imguiLeakRe.IsMatch($line)) {
      Write-Output ('VIOLATION [ui-leak] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
      $violations++
    }
    if (-not $optOut -and $qtColorRe.IsMatch($line)) {
      Write-Output ('VIOLATION [hardcoded-color] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
      $violations++
    }
    if (-not $optOut -and $rel.StartsWith('src/ui/imgui') -and $imguiColorRe.IsMatch($line)) {
      Write-Output ('VIOLATION [hardcoded-color] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
      $violations++
    }
  }
}

if ($violations -eq 0) {
  Write-Output 'check-layers: PASS (0 violations)'
  exit 0
}
Write-Output ('check-layers: FAIL ({0} violations)' -f $violations)
exit 1
