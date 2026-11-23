# tools/check-contrast.ps1 -- 主题色对比度门禁（WCAG 2.1 相对亮度）。
#
# 抓什么：五套主题（src/ui/imgui/theme/Themes/*.json）里「文字/前景 token 落在
# 背景 token 上」的对比度低于阈值。低于 4.5:1 的小字在高 DPI 屏上就是「看不清」——
# 2026-11-23 实测 14 对不达标（text.muted 是全主题通病，最低 3.16），同轮已修。
#
# 用法：
#   powershell -File tools\check-contrast.ps1              # 全套主题审计（违规 exit 1）
#   powershell -File tools\check-contrast.ps1 -SelfTest    # 自检（合成色验证判据本身）
#
# 判据口径：WCAG 相对亮度 + 对比度公式；正文与小字 token 阈值 4.5（本表全部按
# 小字口径取 4.5，不取大字的 3.0 —— 本工程的 muted/hint 全是 10.5~12px 小字）。
# 只审**不透明** token 对（line.*/shadow.* 带 alpha，混色后的对比度取决于底下的
# 叠层，静态审计判不了，跳过并在自检里守住这个边界）。
#
# ⚠️ 本文件必须带 BOM 存 UTF-8（第十九轮同款坑：PowerShell 5.1 读无 BOM 的 UTF-8
#    会按 ANSI 猜编码，中文注释变乱码并报语法错）。
param(
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Luminance([int]$r, [int]$g, [int]$b) {
    $c = @($r, $g, $b) | ForEach-Object {
        $v = $_ / 255.0
        if ($v -le 0.03928) { $v / 12.92 } else { [Math]::Pow(($v + 0.055) / 1.055, 2.4) }
    }
    return 0.2126 * $c[0] + 0.7152 * $c[1] + 0.0722 * $c[2]
}

function Get-Ratio([int]$r1, [int]$g1, [int]$b1, [int]$r2, [int]$g2, [int]$b2) {
    $la = Get-Luminance $r1 $g1 $b1
    $lb = Get-Luminance $r2 $g2 $b2
    $hi = [Math]::Max($la, $lb)
    $lo = [Math]::Min($la, $lb)
    return ($hi + 0.05) / ($lo + 0.05)
}

# "#RRGGBB"（或 "#RRGGBBAA"，取前 6 位）-> R,G,B
function Parse-Hex([string]$hex) {
    $h = $hex.TrimStart('#')
    return @([Convert]::ToInt32($h.Substring(0, 2), 16),
             [Convert]::ToInt32($h.Substring(2, 2), 16),
             [Convert]::ToInt32($h.Substring(4, 2), 16))
}

# (前景 token, 背景 token, 阈值) —— 全部按小字 4.5 口径。
$Pairs = @(
    @('text.primary', 'bg.void', 4.5),
    @('text.primary', 'bg.surface', 4.5),
    @('text.primary', 'bg.panel', 4.5),
    @('text.primary', 'bg.elevated', 4.5),
    @('text.secondary', 'bg.surface', 4.5),
    @('text.secondary', 'bg.panel', 4.5),
    @('text.secondary', 'bg.elevated', 4.5),
    @('text.muted', 'bg.surface', 4.5),
    @('text.muted', 'bg.panel', 4.5),
    @('text.muted', 'bg.elevated', 4.5),
    @('accent.primary', 'bg.surface', 4.5),
    @('accent.primary', 'bg.panel', 4.5),
    @('accent.primary.fg', 'accent.primary', 4.5),
    @('status.ok', 'bg.panel', 4.5),
    @('status.warn', 'bg.panel', 4.5),
    @('status.danger', 'bg.panel', 4.5),
    @('status.busy', 'bg.panel', 4.5)
)

if ($SelfTest) {
    $failed = 0
    # 判据本体的已知读数（真值来自 WCAG 公式手算）：
    #   黑白 = 21.0；#777777 on #FFFFFF = 4.48（差一点就该被判违规）；
    #   #8692A4 on #222B39（深空 text.muted on bg.elevated，本轮修后）>= 4.5。
    $w = @(255, 255, 255)
    $k = @(0, 0, 0)
    $ratio = Get-Ratio $w[0] $w[1] $w[2] $k[0] $k[1] $k[2]
    if ([Math]::Abs($ratio - 21.0) -gt 0.05) { Write-Host "selftest: 黑白对比度应 21.0，实得 $ratio"; $failed++ }
    $g = Parse-Hex '#777777'
    $ratio = Get-Ratio $g[0] $g[1] $g[2] 255 255 255
    if ($ratio -ge 4.5 -or $ratio -lt 4.4) { Write-Host "selftest: #777 on #FFF 应约 4.48，实得 $ratio"; $failed++ }
    $m = Parse-Hex '#8692A4'
    $e = Parse-Hex '#222B39'
    $ratio = Get-Ratio $m[0] $m[1] $m[2] $e[0] $e[1] $e[2]
    if ($ratio -lt 4.5) { Write-Host "selftest: 深空 text.muted on bg.elevated 修后应 >= 4.5，实得 $ratio"; $failed++ }
    # 边界守住：带 alpha 的 token 不进审计表（混色对比度静态判不了）。
    foreach ($p in $Pairs) {
        if ($p[0] -like 'line.*' -or $p[0] -like 'shadow.*' -or $p[1] -like 'line.*' -or $p[1] -like 'shadow.*') {
            Write-Host "selftest: 审计表混进了带 alpha 的 token 对 $($p[0])/$($p[1])"; $failed++
        }
    }
    if ($failed -gt 0) {
        Write-Host "check-contrast selftest: FAIL ($failed)"
        exit 1
    }
    Write-Host "check-contrast selftest: OK (4 checks)"
    exit 0
}

$themes = Get-ChildItem (Join-Path $PSScriptRoot '..\src\ui\imgui\theme\Themes') -Filter '*.json'
$violations = 0
$checked = 0
foreach ($file in $themes) {
    $colors = (Get-Content $file.FullName -Encoding UTF8 -Raw | ConvertFrom-Json).colors
    foreach ($p in $Pairs) {
        $fgHex = $colors.($p[0])
        $bgHex = $colors.($p[1])
        if (-not $fgHex -or -not $bgHex) {
            Write-Host "$($file.BaseName): token 缺失 $($p[0]) / $($p[1])"
            $violations++
            continue
        }
        $fg = Parse-Hex $fgHex
        $bg = Parse-Hex $bgHex
        $ratio = Get-Ratio $fg[0] $fg[1] $fg[2] $bg[0] $bg[1] $bg[2]
        $checked++
        if ($ratio -lt $p[2]) {
            Write-Host ("{0}  [{1}] {2} on {3} = {4} (< {5})" -f `
                $file.BaseName, $p[2], $p[0], $p[1], [Math]::Round($ratio, 2), $p[2])
            $violations++
        }
    }
}
if ($violations -gt 0) {
    Write-Host "check-contrast: FAIL ($violations violations / $checked pairs)"
    exit 1
}
Write-Host "check-contrast: PASS ($($themes.Count) themes, $checked pairs >= 4.5:1)"
exit 0
