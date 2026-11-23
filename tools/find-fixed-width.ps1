# 门禁：依赖文字宽度却写死数字（refactor/component-reuse §7.3 的 R7 第二条）。
#
# ---- 守的规则 ----
# 「凡量宽必走 CalcTextSize / TextSize / LabelWidth」。kit 里**以文字宽度为实参**的
# 入口一共两个（都是居中 / 排版算式）：
#   1. `CenterTextX(x0, x1, textWidth)`      —— 第三实参是文字宽度；
#   2. `PlaceIconText(box, iconSize, gap, textWidth, align, padLeft)`
#      —— 第四实参是文字宽度。
# 这两个位置的实参若是**数字字面量**，等于「假设这段文字永远这么宽」：
#   · 字号缩放档（R6 的 SetUiFontScale）一开，真实宽度就变了，写死的档会错位；
#   · 换文案 / 换语言时布局静默漂移。
# 正确写法：实参走 `CalcTextSizeA(...)` / `TextSize(...)` / `LabelWidth(...)` 的返回值，
# 或事先量好的变量。数字**从变量来**（token / 成员 / 常量表）不算 —— 判据只抓字面量。
#
# ---- 扫描范围：只扫 src/ui/imgui/pages/ ----
# 与 find-raw-text.ps1 同构：kit/ 是这些入口的实现地（内部实参合法），theme/ 是源头，
# verify/ 是取证层 —— 都不在纪律面上。
#
# ---- 存量怎么办：棘轮基线（ratchet），与 find-raw-text.ps1 同一套 ----
# `tools\find-fixed-width.baseline.txt` 按**文件**记存量数（`文件=条数`）。
# 某文件命中数 > 基线 ⇒ 新增违规，exit 1；收编后 -WriteBaseline 收紧，只降不升。
#
# ---- 用法 ----
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\find-fixed-width.ps1
#   powershell ... -File tools\find-fixed-width.ps1 -SelfTest
#   powershell ... -File tools\find-fixed-width.ps1 -Root <目录>
#   powershell ... -File tools\find-fixed-width.ps1 -WriteBaseline
#
# ⚠️ -SelfTest 与 -Root 是改判据后的必跑项：真样本必须逐条命中、假样本一条不命中。
param(
    [string]$Root = '',
    [switch]$SelfTest,
    [string]$SelfTestDir = '',
    [switch]$WriteBaseline
)

# 数字字面量：半点 11.5f / 负号 / 前导点（与 find-raw-text 的字号判据同款）。
$lit = '(?:[-+]?(?:\d+(?:\.\d+)?|\.\d+)f?)'
# CenterTextX(x0, x1, width) —— 第三个实参是数字字面量。
# 单层括号内匹配（实参里出现嵌套括号 ⇒ 肯定是表达式 ⇒ 本来就不命中，无需跨层）。
$rxCenterTextX = [regex]"\bCenterTextX\s*\(\s*[^,()]+,\s*[^,()]+,\s*$lit\s*\)"
# PlaceIconText(box, iconSize, gap, textWidth, …) —— 第四个实参是数字字面量。
$rxPlaceIcon = [regex]"\bPlaceIconText\s*\(\s*[^,()]+,\s*[^,()]+,\s*[^,()]+,\s*$lit\s*,"

# 剥行尾注释 + 掏空字符串字面量：与 find-raw-text.ps1 逐字相同（两份判据共用同一套
# 预处理是刻意的 —— 那边的注释写明「共用同一套」；真要抽公共库，两把门禁一起抽）。
function Remove-LineComment([string]$line) {
    $inStr = $false; $inChr = $false; $esc = $false
    for ($i = 0; $i -lt $line.Length - 1; $i++) {
        $c = $line[$i]
        if ($esc) { $esc = $false; continue }
        if ($c -eq '\') { $esc = $true; continue }
        if ($c -eq '"' -and -not $inChr) { $inStr = -not $inStr; continue }
        if ($c -eq "'" -and -not $inStr) { $inChr = -not $inStr; continue }
        if ($inStr -or $inChr) { continue }
        if ($c -eq '/' -and $line[$i + 1] -eq '/') { return $line.Substring(0, $i) }
    }
    return $line
}
function Remove-StringLiterals([string]$line) {
    $sb = New-Object System.Text.StringBuilder $line.Length
    $inStr = $false; $inChr = $false; $esc = $false
    foreach ($c in $line.ToCharArray()) {
        if ($esc) { [void]$sb.Append(' '); $esc = $false; continue }
        if ($c -eq '\') { [void]$sb.Append($c); $esc = $true; continue }
        if ($c -eq '"' -and -not $inStr) { $inStr = -not $inStr; [void]$sb.Append($c); continue }
        if ($c -eq "'" -and -not $inStr) { $inChr = -not $inChr; [void]$sb.Append($c); continue }
        if ($inStr -or $inChr) { [void]$sb.Append(' '); continue }
        [void]$sb.Append($c)
    }
    return $sb.ToString()
}
function Scan-Text([string]$text) {
    $hits = @()
    $lines = $text -split "`r?`n"
    $inBlock = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $trim = $lines[$i].TrimStart()
        if ($inBlock) {
            if ($trim -match '\*/') { $inBlock = $false }
            continue
        }
        if ($trim.StartsWith('/*')) {
            if ($trim -notmatch '\*/') { $inBlock = $true }
            continue
        }
        if ($trim.StartsWith('//') -or $trim.StartsWith('*')) { continue }
        $code = Remove-StringLiterals (Remove-LineComment $lines[$i])
        if ($rxCenterTextX.IsMatch($code)) { $hits += [pscustomobject]@{ Line = $i + 1; Kind = 'centertextx-literal'; Text = $code.Trim() } }
        if ($rxPlaceIcon.IsMatch($code))   { $hits += [pscustomobject]@{ Line = $i + 1; Kind = 'placeicontext-literal'; Text = $code.Trim() } }
    }
    return $hits
}

# =====================================================================
# 自检：真样本覆盖两个入口 ×（整数 / 半点 / 负号 / 前导点 / 前两参带算术）；
# 假样本覆盖量宽返回值实参、变量实参、token、嵌套括号表达式、三种注释形态、
# 字符串字面量、同形不同入口（CenterTextY 不收宽度实参，不在面上）。
# 期望数按判据现推（见下方 $expect 的推导），不写死不核对样本数。
# =====================================================================
if ($SelfTest) {
    $dir = if ($SelfTestDir) { $SelfTestDir } else { Join-Path $env:TEMP 'fixedwidth-selftest' }
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $enc = New-Object System.Text.UTF8Encoding($false)

    $reals = @(
        'const float x = CenterTextX(a, b, 40.0f);',                    # 字面量（半点）
        'CenterTextX(box.min.x, box.max.x, 12);',                       # 字面量（整数）
        'CenterTextX(a + 1.0f, b - 2.0f, -8.0f);',                      # 前两参带算术 + 负号
        'CenterTextX(a, b, .5f);',                                      # 前导点
        'PlaceIconText(box, 14.0f, 4.0f, 40.0f, IconTextAlign::Center);',  # 第四参字面量
        'PlaceIconText(box, icon, gap, 96.0f, align, padLeft);',        # 第四参字面量（变量前参）
        'kit::CenterTextX(a, b, 20.0f);'                                # 带命名空间前缀
    )
    $fakes = @(
        'const float x = CenterTextX(a, b, LabelWidth(f, 12.5f, t));',  # 量宽返回值
        'CenterTextX(a, b, textWidth);',                                # 变量
        'CenterTextX(a, b, theme::font::kBody);',                       # token（类型上不成立，判据也不抓）
        'const float w = CenterTextX(a, b, w1 + w2);',                  # 表达式（嵌套 ⇒ 单层括号判据不中）
        'PlaceIconText(box, 14.0f, 4.0f, textW, align);',               # 第四参是变量
        '// CenterTextX(a, b, 40.0f) 行注释不许报',
        '/* CenterTextX(a, b, 40.0f) 块注释不许报 */',
        'const char* k = "CenterTextX(a, b, 40.0f)";',                  # 字符串字面量
        'const float y = CenterTextY(f, 12.0f, cy);'                    # 同形不同入口（CenterTextY 不在面上）
    )
    [IO.File]::WriteAllBytes((Join-Path $dir 'samples.cpp'), $enc.GetBytes((($reals + $fakes) -join "`n")))

    $got = @(& $PSCommandPath -Root $dir 2>&1 |
             Select-String -Pattern 'fixed-width hits: (\d+)')
    $n = if ($got.Count -gt 0) { [int]$got[0].Matches[0].Groups[1].Value } else { -1 }
    Remove-Item $dir -Recurse -Force
    # 期望数按判据现推：真样本 7 行逐条命中 = 7；假样本 9 条一条不中（CenterTextY
    # 的 12.0f 是**字号**不是宽度，第三实参判据只认 CenterTextX 的三参形）。
    $expect = 7
    if ($n -ne $expect) {
        Write-Output "find-fixed-width 自检 FAILED：期望命中 $expect，实得 $n"
        exit 1
    }
    Write-Output "find-fixed-width 自检 PASS（7 真 + 9 假样本，命中数 $n = 期望 $expect）"
    exit 0
}

# =====================================================================
# 正常路径（与 find-raw-text.ps1 同构：逐文件扫 → 棘轮基线）
# =====================================================================
$ErrorActionPreference = 'Continue'
$pagesRoot = if ($Root) { $Root } else { Join-Path $PSScriptRoot '..\src\ui\imgui\pages' }
if (-not (Test-Path $pagesRoot)) {
    Write-Output "find-fixed-width: 找不到 $pagesRoot"
    exit 1
}

$perFile = @{}
$all = @()
Get-ChildItem $pagesRoot -Recurse -Include *.h,*.hpp,*.cpp -File | ForEach-Object {
    $file = $_
    $rel = [IO.Path]::GetFileName($file.FullName)
    $text = [IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8)
    $hits = @(Scan-Text $text)
    if ($hits.Count -gt 0) {
        $perFile[$rel] = $hits.Count
        foreach ($h in $hits) { $all += [pscustomobject]@{ File = $rel; Line = $h.Line; Kind = $h.Kind; Text = $h.Text } }
    }
}

$baselinePath = Join-Path $PSScriptRoot 'find-fixed-width.baseline.txt'
$baseline = @{}
if (Test-Path $baselinePath) {
    foreach ($ln in (Get-Content $baselinePath -Encoding UTF8)) {
        $t = $ln.Trim()
        if (-not $t -or $t.StartsWith('#')) { continue }
        $kv = $t -split '='
        if ($kv.Count -eq 2) { $baseline[$kv[0].Trim()] = [int]$kv[1].Trim() }
    }
}

if ($WriteBaseline) {
    $lines = @('# find-fixed-width 棘轮基线：文件=存量条数。收编一个文件就跑 -WriteBaseline 收紧，只降不升。')
    foreach ($k in ($perFile.Keys | Sort-Object)) { $lines += "$k=$($perFile[$k])" }
    [IO.File]::WriteAllLines($baselinePath, $lines, (New-Object System.Text.UTF8Encoding($true)))
    Write-Output "find-fixed-width: 基线已写入 $baselinePath（$($perFile.Count) 个文件，共 $(($perFile.Values | Measure-Object -Sum).Sum) 条）"
    exit 0
}

foreach ($h in $all) {
    "{0}:{1}  [{2}]" -f $h.File, $h.Line, $h.Kind
    "    $($h.Text)"
}
''
"--- fixed-width hits: $($all.Count) ---"

$newViolations = @()
foreach ($k in ($perFile.Keys | Sort-Object)) {
    $b = if ($baseline.ContainsKey($k)) { $baseline[$k] } else { 0 }
    if ($perFile[$k] -gt $b) {
        $newViolations += "$k：现 $($perFile[$k]) / 基线 $b（新增 $($perFile[$k] - $b)）"
    } elseif ($perFile[$k] -lt $b) {
        "    改善：$k 现 $($perFile[$k]) / 基线 $b —— 收编后跑 -WriteBaseline 把基线收到 $($perFile[$k])"
    }
}

if ($newViolations.Count -gt 0) {
    Write-Output ''
    Write-Output "--- NEW violations: $($newViolations.Count) 个文件超过基线 ---"
    foreach ($v in $newViolations) { Write-Output "    $v" }
    Write-Output '量宽必须走 CalcTextSize / TextSize / LabelWidth —— CenterTextX 第三参 / PlaceIconText 第四参不许是数字字面量。'
    exit 1
}
if ($all.Count -gt 0) {
    Write-Output "find-fixed-width: PASS（$($all.Count) 条存量全部在基线内；收编后跑 -WriteBaseline 收紧棘轮）"
} else {
    Write-Output 'find-fixed-width: PASS（0 处写死文字宽度）'
}
exit 0
