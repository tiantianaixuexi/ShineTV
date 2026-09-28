# check-layers.ps1 -- ShineTV Studio layering gate (P01-S7)
#
# Rule 1: shine_core modules must never include Qt headers
#         (only the src/ui tree and its layout helpers may use Qt).
# Rule 2: no legacy-UI leftovers in C/C++ sources
#         (imgui / ImVec / ImDraw / VisNodeSys / ImAnim / VisualNode,
#          case-insensitive -- the S1 baseline pattern plus VisualNode).
# Rule 3: no inline hardcoded color literals in style code
#         (setStyleSheet("...#RRGGBB") and QColor("#...") style;
#          colors live in theme tokens, see src/ui/kit/theme/Token.h).
#
# Scope: code under src/ only. Project documentation is maintained under docs/;
# this gate intentionally scans source files only.
#
# Usage: pwsh -File tools/check-layers.ps1
# Exit code 0 = pass, 1 = violations found.
# Scripts are pure ASCII on disk (see Plan risk notes, section 5).

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# shine_core module dirs (per the module map in Plan API.md) + gpu: zero Qt allowed.
$coreDirs = @(
  'src/core', 'src/util', 'src/net', 'src/db', 'src/comfy',
  'src/media', 'src/flow', 'src/novel', 'src/visual', 'src/paint',
  'src/mcp', 'src/llm', 'src/project', 'src/pipeline', 'src/gpu'
)

$legacyRe = [regex]'(?i)imgui|ImVec|ImDraw|VisNodeSys|ImAnim|VisualNode'
$colorRe = [regex]'(?i)(setStyleSheet|QColor)\s*\(?[^)]*#[0-9A-F]{6,8}'

$violations = 0

$codeFiles = Get-ChildItem -LiteralPath (Join-Path $root 'src') -Recurse -File |
  Where-Object { $_.Extension -in '.h', '.hpp', '.cpp', '.cc' }

foreach ($f in $codeFiles) {
  $rel = $f.FullName.Substring($root.Length + 1) -replace '\\', '/'
  $inCore = $false
  foreach ($d in $coreDirs) {
    if ($rel.StartsWith($d)) { $inCore = $true; break }
  }
  $lines = [System.IO.File]::ReadAllLines($f.FullName)
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    $isQtUtil = $rel -like 'src/util/Qt*'
    if ($inCore -and -not $isQtUtil -and $line -cmatch '#include\s*<Q[A-Za-z]') {
      Write-Output ('VIOLATION [qt-include] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
      $violations++
    }
    if ($legacyRe.IsMatch($line)) {
      Write-Output ('VIOLATION [legacy-ui] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
      $violations++
    }
    if ($colorRe.IsMatch($line)) {
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
