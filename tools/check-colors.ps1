# 门禁：禁止在 src/ 里硬编码颜色字面量。
#
# ---- 为什么这个门禁曾经是废的（别删掉这段历史）----
# 老版本只匹配 `setStyleSheet` / `QColor` —— 那是 **Qt 时代**的模式。P7 把整棵 Qt 树
# 删掉之后，这两个词在全仓**一个都不存在**，于是这个门禁**永远不可能失败**，却一直
# 报 "PASS (0 hard-coded style colors)"。一条永远不会失败的门禁比没有门禁更坏：
# 它让人以为「颜色这条线有机器守着」，于是真写进去时没人拦。
#
# 现在守的规则是当前代码库真正遵守的那条：颜色一律走
# `kit::ColorXxx()` / `kit::ColorOf(theme::Current()...)` / `theme::CurrentDerived()`。
#
# ---- 怎么算「硬编码颜色字面量」----
#   - IM_COL32(r, g, b, a)      —— 四个/三个参数全是数字
#   - ImVec4(r, g, b[, a])      —— 全部参数是数字字面量
#   - ImU32(0xAARRGGBB)         —— 8 位十六进制
#   - "#rrggbb" / "#rrggbbaa"   —— 字符串里的十六进制色
#   - 裸 0xAARRGGBB
#
# 注释里提到颜色**不算**：本仓库的注释大量在解释「这里原来写的是 #xxx」，
# 报出来会逼着人把解释删掉。整行注释直接跳过；行尾注释先剥掉再匹配
# （`draw(..., IM_COL32(...)); // 以前是红色` 仍会被报出来 —— 代码部分才是重点）。
#
# ---- 豁免 ----
# 两个出口，**都必须写理由**（没有理由的豁免无效，等于把门禁关掉）：
#   1. 命中行往上 6 行内：`// theme-ok <理由>`
#   2. 命中文件在下面的 $Zones 里，且该条写了理由
#
# 豁免项照常打印（单列一节），不静默：被豁免的位置要让后来人看得见。
#
# 用法:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\check-colors.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\check-colors.ps1 -SelfTest
param(
    [switch]$SelfTest,
    [string]$SelfTestDir = ''
)

# ---- 允许硬编码颜色的文件（每条必须写清楚为什么）----
# 不用「按目录排除」，因为目录排除会静默吞掉将来新加的违规文件；列到具体文件，
# 新文件要进来就得有人明着改这张表。
# 路径相对 $root（即 src/ui/imgui）。
$Zones = @(
    @{
        Rel  = 'theme/Theme.cpp'
        Why  = '它就是调色板本身：这一段把 token 赋给 ImGuiCol_*，是「颜色从哪来」的源头'
    },
    @{
        Rel  = 'kit/Views.cpp'
        Why  = '设计稿 UI.jsx:184 的 12 组插画调色板（深/中/亮），是**数据表**不是界面配色；数据表本来就该把色值写死'
    }
)

$ErrorActionPreference = 'Continue'
# ---- 扫描范围：只扫 src/ui/imgui（不是整个 src/）----
# 这不是「把范围缩小到能过」—— 规则本身就只对 UI 成立：颜色字面量出现在绘制代码里
# 才是违规，而绘制代码只可能在 ImGui 前端。`tools/check-layers.ps1` 已经保证
# `shine_core` 里不可能有 ImGui 头，也就同样不可能有「画一个控件时硬编码颜色」。
# 早先扫 `src/` 是 Qt 时代的遗留（那时 UI 散在 `src/ui/{app,kit,layout,pages}`），
# 代价是拿业务层的整数常量当颜色报：`src/media/VideoThumb.cpp` 的
# `static_cast<HRESULT>(0x8000000A)`（E_ABORT）就是这样一个恒定的假阳性 ——
# 而那个文件属于 shine_core，本轮不能动它。留在这里是为了让后来人知道范围为什么
# 是这样，别再悄悄改回去。
$root = Join-Path $PSScriptRoot '..\src\ui\imgui'


# 注释之外的颜色字面量。IM_COL32 / ImVec4 的实参必须全是数字，才算硬编码 ——
# `IM_COL32(ColorAccent())` 是取色不是写色。
$rxImColor = [regex]'IM_COL32\s*\(\s*(?:-?[\d.]+f?|0[xX][0-9A-Fa-f]+)\s*(?:,\s*(?:-?[\d.]+f?|0[xX][0-9A-Fa-f]+)\s*){2,3}\)'
$rxVec4 = [regex]'ImVec4\s*\(\s*-?[\d.]+f?\s*,\s*-?[\d.]+f?\s*,\s*-?[\d.]+f?\s*(?:,\s*-?[\d.]+f?\s*)?\)'
$rxHexStr = [regex]'#(?:[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})\b'
$rxHexNum = [regex]'\b0[xX][0-9A-Fa-f]{8}\b'

# 剥掉行尾注释：扫一遍字符，'//' 只有不在字符串/字符字面量里时才算注释起点。
# （naive 的 IndexOf('//') 会把 "https://…" 里的两个斜杠当注释，误伤一整行代码。）
function Remove-LineComment([string]$line) {
    $inStr = $false; $inChr = $false; $esc = $false
    for ($i = 0; $i -lt $line.Length - 1; $i++) {
        $c = $line[$i]
        if ($esc) { $esc = $false; continue }
        if ($c -eq '\') { $esc = $true; continue }
        if ($c -eq '"' -and -not $inChr) { $inStr = -not $inStr; continue }
        if ($c -eq "'" -and -not $inStr) { $inChr = -not $inChr; continue }
        if ($inStr -or $inChr) { continue }
        if ($c -eq '/' -and $line[$i + 1] -eq '/') { return $line.Substring(0, $i) }
    }
    return $line
}

function Scan-Text([string]$text, [string]$fileName) {
    $hits = @()
    $lines = $text -split "`r?`n"
    $inBlockComment = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        $trim = $line.TrimStart()

        # 块注释状态机（跨行）。
        if ($inBlockComment) {
            if ($trim -match '\*/') { $inBlockComment = $false }
            continue
        }
        if ($trim.StartsWith('/*')) {
            if ($trim -notmatch '\*/') { $inBlockComment = $true }
            continue
        }
        # 整行注释
        if ($trim.StartsWith('//') -or $trim.StartsWith('*')) { continue }
        # 预处理器指令：行首 `#` 不是颜色（`#include` / `#define` 里的字母不构成 6 位十六进制，
        # 但 `#error 0xDEADBEEF` 这类会，所以显式放行整行最省事）
        if ($trim.StartsWith('#')) { continue }

        $code = Remove-LineComment $line
        if (-not $code.Trim()) { continue }

        $kind = $null
        if ($rxImColor.IsMatch($code)) { $kind = 'IM_COL32-literal' }
        elseif ($rxVec4.IsMatch($code)) { $kind = 'ImVec4-literal' }
        elseif ($rxHexNum.IsMatch($code)) { $kind = '0xAARRGGBB-literal' }
        elseif ($rxHexStr.IsMatch($code)) { $kind = 'hex-string' }
        if (-not $kind) { continue }

        # 显式豁免：`theme-ok <理由>` —— 同行行尾，或往上 6 行内。
        # 两种位置都认：代码库已有的约定是**行尾**（Tokens.h 的 shadow 表、Theme.cpp 的
        # ImGuiCol_* 表都是 `};  // theme-ok`），而 find-silent-truncation.ps1 用的是
        # 往上若干行。只认一种会让另一个门禁已经接纳的写法在这里变成违规。
        #
        # ⚠️ 从命中行**往上**找，第一个命中的就是**最近**的那条 —— 方向不能反。
        #    反过来（从 i-6 往 i 扫）先撞到的是最远的一条，于是「解释这个豁免机制的
        #    注释」会抢走真正理由的位置，报告里印出一句驴唇不对马嘴的「理由」，
        #    而门禁照样判绿。理由错了，豁免记录就等于没有记录。
        $suppress = $null
        for ($k = $i; $k -ge [Math]::Max(0, $i - 6); $k--) {
            $m = [regex]::Match($lines[$k], 'theme-ok\s*(.*)$')
            if ($m.Success) {
                $reason = $m.Groups[1].Value.Trim().TrimEnd('*/').Trim()
                if ($reason) { $suppress = $reason }
                else { $suppress = '(未写理由 —— 豁免无效)' }
                break
            }
        }
        $hits += [pscustomobject]@{
            File = $fileName
            Line = $i + 1
            Kind = $kind
            Text = $code.Trim()
            Suppress = $suppress
        }
    }
    return $hits
}

# =====================================================================
# 自检：一个「改完报 0」的扫描器和一个坏掉的没区别。
# 11 份样本，覆盖三件事：
#   - 匹配逻辑：3 真（IM_COL32 / ImVec4 / hex 串）5 假（取色调用、整行注释、行尾注释、
#     预处理器行、跨行块注释）
#   - 豁免逻辑：**带理由**才放行、**没理由照样算命中**
#   - 豁免取最近的那条 `theme-ok`（下面 exempt-nearest 这份就是防「抢到最远那条」
#     那个 bug 复发的，改那段代码后自检必须还是 PASS）
# =====================================================================
if ($SelfTest) {
    $dir = if ($SelfTestDir) { $SelfTestDir } else { Join-Path $env:TEMP 'colorscan-selftest' }
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $enc = New-Object System.Text.UTF8Encoding($false)

    $samples = @(
        @{ Name = 'true-imcolor.cpp'; Text = 'draw->AddRectFilled(p, q, IM_COL32(255, 0, 0, 255));'; Expect = $true },
        @{ Name = 'true-imvec4.cpp';  Text = 'auto c = ImVec4(0.2f, 0.3f, 0.4f, 1.0f);'; Expect = $true },
        @{ Name = 'true-hexstr.cpp';  Text = 'const char* s = "#e86a8b";'; Expect = $true },
        @{ Name = 'false-token.cpp';  Text = 'ImU32 ColorAccent() { return ColorOf(theme::Current().accentPrimary); }'; Expect = $false },
        @{ Name = 'false-comment.cpp'; Text = "// 旧代码写的是 #ff0000，这整行只是注释"; Expect = $false },
        @{ Name = 'false-trailing.cpp'; Text = 'draw->AddText(d, f, p, ColorTextMuted(), "x");  // 注释里提 #123456'; Expect = $false },
        @{ Name = 'false-include.cpp'; Text = '#include "ui/imgui/kit/Draw.h"'; Expect = $false },
        @{ Name = 'false-block.cpp';  Text = "/* 设计稿写的是 IM_COL32(1, 2, 3, 4)`n   这里是块注释 */`nint real = 7;"; Expect = $false },
        # 有理由 ⇒ 放行，且理由必须是**最近**那条
        @{ Name = 'exempt-nearest.cpp'
           Text = "// theme-ok 这条只是解释豁免机制的注释，不该抢走理由`n//`n//`n//`n//`n//`n//`n//`nauto c = ImVec4(0.2f, 0.3f, 0.4f, 1.0f);  // theme-ok 真正的理由"
           Expect = $false; Reason = '真正的理由' },
        # 有标记但没理由 ⇒ 无效豁免，照样算命中
        @{ Name = 'exempt-noreason.cpp'
           Text = 'auto c = ImVec4(0.2f, 0.3f, 0.4f, 1.0f);  // theme-ok'
           Expect = $true }
    )
    foreach ($s in $samples) {
        [IO.File]::WriteAllBytes((Join-Path $dir $s.Name), $enc.GetBytes($s.Text))
    }

    $fail = 0
    foreach ($s in $samples) {
        $t = [IO.File]::ReadAllText((Join-Path $dir $s.Name), [Text.Encoding]::UTF8)
        $h = @(Scan-Text $t $s.Name)
        # 「命中」= 报出来且**没有**有效豁免。理由为空的无效豁免仍然算命中 ——
        # 这正是本门禁的纪律：一个没有理由的豁免等于把门禁关掉。
        $effective = @($h | Where-Object { $null -eq $_.Suppress -or $_.Suppress.StartsWith('(') })
        $isHit = $effective.Count -gt 0
        $ok = ($isHit -eq $s.Expect)
        if ($ok -and $s.ContainsKey('Reason')) {
            $got = if ($h.Count -gt 0) { [string]$h[0].Suppress } else { '<无>' }
            if ($got -ne $s.Reason) { $ok = $false }
        }
        if (-not $ok) { $fail++ }
        Write-Output ("{0,-22} 期望{1}  实得{2}  {3}{4}" -f $s.Name,
            $(if ($s.Expect) { '命中' } else { '放行' }), $(if ($isHit) { '命中' } else { '放行' }),
            $(if ($ok) { 'OK' } else { '✗ 不符' }),
            $(if ($s.ContainsKey('Reason')) { "  理由=$($h[0].Suppress)" } else { '' }))
    }
    Remove-Item $dir -Recurse -Force
    if ($fail -gt 0) {
        Write-Output "check-colors 自检 FAILED：$fail 份样本与期望不符"
        exit 1
    }
    Write-Output 'check-colors 自检 PASS（11 份样本：3 真 + 5 假 + 带理由豁免 + 无理由豁免 + 最近理由）'
    exit 0
}

# =====================================================================
# 正常路径：扫 src/
# =====================================================================
if (-not (Test-Path $root)) {
    Write-Output "check-colors: 找不到 $root"
    exit 1
}
# Zone 的相对路径是按 src/ 写的，这里换算成绝对路径并校验文件还在 ——
# 列表里留一条指向已删文件的豁免，等于那文件的规则悄悄失效了。
$zoneMap = @{}
foreach ($z in $Zones) {
    $full = [IO.Path]::GetFullPath((Join-Path $root $z.Rel))
    if (-not (Test-Path $full)) {
        Write-Output "check-colors: 豁免区指向的文件不存在：$($z.Rel)（删文件时要同步删这条豁免）"
        exit 1
    }
    if (-not $z.Why -or $z.Why.Trim().Length -lt 8) {
        Write-Output "check-colors: 豁免区 $($z.Rel) 没写理由，豁免无效"
        exit 1
    }
    $zoneMap[$full] = $z.Why
}

$hits = @()
$suppressed = @()
Get-ChildItem $root -Recurse -Include *.h,*.hpp,*.cpp -File | ForEach-Object {
    $file = $_
    $text = [IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8)
    $zoneWhy = $null
    if ($zoneMap.ContainsKey($file.FullName)) { $zoneWhy = $zoneMap[$file.FullName] }
    foreach ($h in (Scan-Text $text $file.Name)) {
        if ($zoneWhy) { $h.Suppress = "豁免区：$zoneWhy" }
        if ($h.Suppress -and $h.Suppress -notlike '(*)') { $suppressed += $h } else { $hits += $h }
    }
}

if ($suppressed.Count -gt 0) {
    Write-Output "--- acknowledged (not counted) : $($suppressed.Count) ---"
    foreach ($h in $suppressed) {
        Write-Output "$($h.File):$($h.Line)  [$($h.Kind)]  理由: $($h.Suppress)"
    }
    Write-Output ''
}

$themeFiles = Get-ChildItem (Join-Path $root 'theme/Themes') -Filter *.json -File
if ($themeFiles.Count -lt 4) {
    Write-Output "check-colors: 期望四份主题文件，实际 $($themeFiles.Count)"
    exit 1
}

if ($hits.Count -gt 0) {
    Write-Output "--- hard-coded color literals: $($hits.Count) ---"
    foreach ($h in $hits) {
        Write-Output "$($h.File):$($h.Line)  [$($h.Kind)]"
        Write-Output "    $($h.Text)"
    }
    Write-Output ''
    Write-Output '颜色必须走 kit::ColorXxx() / ColorOf(theme::Current()...) / CurrentDerived()。'
    Write-Output '「不画」用 kit::ColorTransparent()（已经补了这个出口，别再写 IM_COL32(0,0,0,0)）。'
    Write-Output '确实该写死的，在命中行往上 6 行内写：// theme-ok <理由>'
    exit 1
}

Write-Output "check-colors: PASS ($($themeFiles.Count) themes, 0 hard-coded color literals, $($suppressed.Count) acknowledged)"
exit 0
