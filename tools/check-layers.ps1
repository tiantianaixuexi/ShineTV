# check-layers.ps1 -- ShineTV Studio layering gate
#
# Qt -> Dear ImGui refactor, step P0.3. The step list that used to live in
# refactor/phases.md was removed with the rest of that tree (commit ddab612);
# the gate itself is the only thing that has to keep working. Three rules:
#
# Rule 1 [qt-free]: no Qt header may appear anywhere under src/.
#         P7 deleted the whole legacy Qt front end (src/ui/{app,kit,layout,
#         pages,qml,verify}), so the exemption those dirs used to carry is
#         gone with them. src/ is now entirely Qt-free, shine_core and the
#         ImGui front end alike.
#
# Rule 2: (removed) the old "no imgui/ImVec/ImDraw anywhere in src/" ban.
#         ImGui is now the front end; imgui.h / ImVec4 / ImDrawList are
#         ordinary headers here. What is still banned is the reverse leak:
#         shine_core must not include ImGui headers (business layer stays
#         host-agnostic and free of any UI dependency).
#
# Rule 3 [hardcoded-color]: colors come from the theme token table, never
#         from literals -- ImVec4(0.1f, ...) / ImColor(0x...) / 0xRRGGBB.
#         A line may opt out with a trailing `theme-ok` marker, which is
#         how theme/Tokens.* legitimately hold the source-of-truth values.
#         Comment lines are skipped: a comment cannot change the build, and
#         this repo's comments routinely quote the old bad literal so the next
#         reader knows what was replaced ("the former value was ImVec4(0.1f,
#         0.2f, 0.3f, 1.0f)"). Reporting those pushes people to delete the
#         explanation. tools/find-rect-wh-misuse.ps1 already learned this.
#
# Scope: code under src/ only.
# Usage: pwsh -File tools/check-layers.ps1
#        pwsh -File tools/check-layers.ps1 -SelfTest
# Exit code 0 = pass, 1 = violations found.
# Script is pure ASCII on disk.
param(
    [switch]$SelfTest,
    [string]$SelfTestDir = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Directories that must stay free of ANY UI header (business layer).
$coreDirs = @(
  'src/core', 'src/util', 'src/net', 'src/db', 'src/comfy',
  'src/media', 'src/flow', 'src/novel', 'src/visual', 'src/paint',
  'src/mcp', 'src/llm', 'src/project', 'src/pipeline', 'src/gpu'
)

# Case-sensitive on purpose: <Q... must be a Qt header, not <queue> / <QtMath-like>.
# This mirrors the original gate's -cmatch behaviour.
$qtIncludeRe = [regex]'#include\s*[<"]Q[A-Za-z]|^\s*Q_OBJECT\b'
$imguiLeakRe = [regex]'(?i)^(imgui|ImGui|ImDraw)'
$qtColorRe = [regex]'(?i)(setStyleSheet|QColor)\s*\(?[^)]*#[0-9A-F]{6,8}'
# ImVec4(0.1f, ...) / ImVec4(0, 0, 0, 1) / ImColor(0x35D0B4) / 0xRRGGBB literals.
$imguiColorRe = [regex]'(?i)ImVec4\s*\(\s*[0-9]|ImColor\s*\(\s*[0-9]|\b0x[0-9A-F]{8}\b'
# -- Only the path is captured here; the "is it a UI header" test is done below
#    on the LAST path segment. The old pattern was
#    `#include\s*[<"](imgui|ImDraw|ImGui)[>"]`, which requires the header name to
#    be followed immediately by `>`. Real includes are `#include "imgui.h"` --
#    the `.h` breaks the match, so **shine_core including ImGui was never
#    detected**. That is the single most important layering rule in this repo
#    (business layer must stay host-agnostic) and it did not fire on the only
#    spelling that occurs. Caught by -SelfTest, not by reading the code.
$includeRe = [regex]'(?i)#include\s*[<"](?<path>[^>"]+)[>"]'

$violations = 0

# True when this line is comment-only, or sits inside a /* */ block.
# Kept as a tiny state machine so a multi-line block comment is handled too --
# a per-line StartsWith('*') check misses the middle lines.
function Test-IsCommentLine([string[]]$lines, [int]$i, [ref]$inBlock) {
    $t = $lines[$i].TrimStart()
    if ($inBlock.Value) {
        if ($t -match '\*/') { $inBlock.Value = $false }
        return $true
    }
    if ($t.StartsWith('/*')) {
        if ($t -notmatch '\*/') { $inBlock.Value = $true }
        return $true
    }
    return ($t.StartsWith('//') -or $t.StartsWith('*'))
}

function Invoke-Gate([string]$base, [string]$scanRoot) {
    $script:violations = 0
    $codeFiles = Get-ChildItem -LiteralPath (Join-Path $scanRoot 'src') -Recurse -File |
        Where-Object { $_.Extension -in '.h', '.hpp', '.cpp', '.cc' }

    foreach ($f in $codeFiles) {
        $rel = $f.FullName.Substring($scanRoot.Length + 1) -replace '\\', '/'
        $inCore = $false
        foreach ($d in $script:coreDirs) {
            if ($rel.StartsWith($d)) { $inCore = $true; break }
        }
        $isQtUtil = $rel -like 'src/util/Qt*'

        $lines = [System.IO.File]::ReadAllLines($f.FullName)
        $inBlock = $false
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $line = $lines[$i]
            $isComment = Test-IsCommentLine $lines $i ([ref]$inBlock)
            # Includes are checked on every line: a commented-out #include is
            # still worth flagging if it was ever meant to be live, and the
            # false-positive cost there is nil.
            $optOut = $line -match 'theme-ok'
            if (-not $isQtUtil -and $qtIncludeRe.IsMatch($line)) {
                Write-Output ('VIOLATION [qt-include] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
                $script:violations++
            }
            if ($inCore) {
                $inc = $includeRe.Match($line)
                if ($inc.Success) {
                    $leaf = ($inc.Groups['path'].Value -split '[/\\]')[-1]
                    if ($imguiLeakRe.IsMatch($leaf)) {
                        Write-Output ('VIOLATION [ui-leak] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
                        $script:violations++
                    }
                }
            }
            if ($isComment) { continue }
            if (-not $optOut -and $qtColorRe.IsMatch($line)) {
                Write-Output ('VIOLATION [hardcoded-color] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
                $script:violations++
            }
            if (-not $optOut -and $rel.StartsWith('src/ui/imgui') -and $imguiColorRe.IsMatch($line)) {
                Write-Output ('VIOLATION [hardcoded-color] {0}:{1}: {2}' -f $rel, ($i + 1), $line.Trim())
                $script:violations++
            }
        }
    }
    return $script:violations
}

# A gate that always passes is worse than no gate: it reads as "this line has a
# machine watching it". The self-test proves the rules still fire, and that the
# comment-skip does not swallow real code.
if ($SelfTest) {
    $dir = if ($SelfTestDir) { $SelfTestDir } else { Join-Path $env:TEMP 'layers-selftest' }
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    $enc = New-Object System.Text.UTF8Encoding($false)
    $mk = {
        param($rel, $text)
        $full = Join-Path $dir $rel
        $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $full)
        [System.IO.File]::WriteAllBytes($full, $enc.GetBytes($text))
    }
    & $mk 'src/ui/imgui/kit/A.cpp' "draw->AddRectFilled(a, b, ImVec4(0.2f, 0.3f, 0.4f, 1.0f));"
    & $mk 'src/ui/imgui/kit/B.cpp' "auto c = ImColor(0x35D0B4);"
    & $mk 'src/ui/imgui/kit/C.cpp' "constexpr unsigned kTint = 0xFF00FF00;"
    & $mk 'src/ui/imgui/kit/D.cpp' "#include <QWidget>"
    & $mk 'src/core/E.cpp' "#include `"imgui.h`""
    & $mk 'src/core/E2.cpp' "#include `"../third/imgui/imgui.h`""
    & $mk 'src/ui/imgui/kit/F.cpp' "// old value was ImVec4(0.1f, 0.2f, 0.3f, 1.0f) -- now a token"
    & $mk 'src/ui/imgui/kit/G.cpp' "/*`n   block comment`n   ImVec4(0, 0, 0, 1) inside`n*/`nint real = 7;"
    & $mk 'src/ui/imgui/kit/H.cpp' "ImVec4(0, 0, 0, 1),  // theme-ok design spec constant"
    & $mk 'src/ui/imgui/kit/I.cpp' "#include `"imgui.h`""
    & $mk 'src/core/J.cpp' "#include `"core/Log.h`""

    # -- The violation lines go to the output stream too, so the caller's
    #    $got would be an ARRAY (messages + count), not a number. The count is
    #    read back from $script:violations and the messages are discarded with
    #    Out-Null. Returning "everything" from a function that also prints is the
    #    PowerShell default and it is exactly what made this self-test report
    #    "got System.Object[]".
    $msgs = @(Invoke-Gate $root $dir)
    $got = $script:violations
    # A/B/C = hardcoded color under src/ui/imgui, D = Qt include,
    # E/E2 = ui-leak in core (bare and nested path).
    $expect = 6
    Remove-Item $dir -Recurse -Force
    if ($got -ne $expect) {
        Write-Output ("check-layers self-test FAILED: expected {0} violations, got {1}" -f $expect, $got)
        $msgs | ForEach-Object { Write-Output ("  got: {0}" -f $_) }
        exit 1
    }
    Write-Output ('check-layers self-test PASS ({0} violations, comments and theme-ok correctly skipped)' -f $expect)
    exit 0
}

Invoke-Gate $root $root | Out-Null
$violations = $script:violations

if ($violations -eq 0) {
  Write-Output 'check-layers: PASS (0 violations)'
  exit 0
}
Write-Output ('check-layers: FAIL ({0} violations)' -f $violations)
exit 1
