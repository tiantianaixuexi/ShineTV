# 找「把宽高写进 kit::Rect 四参构造」的误用。
#
# kit::Rect(float,float,float,float) 是 (minX, minY, maxX, maxY)，不是 (x, y, w, h)。
# 写错不报编译错：max < min 时 DrawRoundRect / HitTest 直接 return，控件整个不画也不可点。
#
# 判据（保守，宁可多报）：取出每个 `Rect{...}` 的**顶层**四个实参，
#   - 恰好 4 个顶层实参（2 参与 ImVec2 形式不在此列）
#   - 第 3 个实参是「尺寸样」：**标识符以 W/w/H/h 结尾**（含 Width/Height/cardW/colW/
#     cellW/iconW/thumbH，以及单字母的 w/h），或裸浮点字面量
#   - 第 4 个实参是裸浮点字面量
# 命中只说明「疑似」，要人眼看一眼：第 3/4 参若是 x+w / y+h 这种就是合法的 min/max。
#
# ⚠️ 第 3 条原先写的是 `(?i)(width|height|cardW|panelW|boxW|\d+f?$)` —— 只认那几个
#    具体的名字，于是 `w` / `h` / `colW` / `cellW` / `iconW` / `thumbH` **全部漏网**，
#    而这正是上面那行注释早就承诺要覆盖的形态（「变量名以 W/w/H/h 结尾」）。
#    **文档说覆盖了、代码没覆盖** —— 与 check-colors.ps1 漏 `ImU32(0x…u)` 是同一类，
#    所以判据改成按**结尾字符**判，而不是按名字枚举。枚举永远漏。
#
# ⚠️ **纯字面量的合法 min/max 会被报出来，这是有意的保守取舍。**
#    `Rect{0.0f, 0.0f, 96.0f, 32.0f}` 是完全合法的 (minX,minY,maxX,maxY)，但它同时
#    满足上面两条判据（第 3 参是裸浮点、第 4 参是裸浮点）—— 语法层面无法与
#    `Rect{x, y, cardW, 192.0f}` 区分。宁可多报也不漏报（漏报的后果是控件整张不画）。
#    真实树报 0 只是因为那里合法的 min/max 第 4 参几乎都不是裸字面量。
#
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File tools\find-rect-wh-misuse.ps1
#       powershell ... -File tools\find-rect-wh-misuse.ps1 -SelfTest
#       powershell ... -File tools\find-rect-wh-misuse.ps1 -Root <目录>
#
# -SelfTest 造一份含真/假样本的临时目录，断言「命中数 == 真样本数」并打印 PASS/FAIL。
# 一个「改完报 0」的扫描器和一个坏掉的没区别，所以自检是**改这个文件后的必跑项**。
#
# ⚠️ `-SelfTest` 曾经**不存在**：把它传进来会被静默忽略，然后照常扫默认根目录并打印
#    `--- suspected width/height misuse: 0 ---` —— 与「真通过」字面完全相同。
#    那是这个脚本自己的假绿，比它要抓的缺陷更坏。
#
# ⚠️ 自检样本里的「合法样本」必须用**末参非裸字面量**的形态（`Rect{a.min.x,…,a.max.y}`）
#    或走 `RectAt(...)`。拿纯字面量 min/max 当合法样本，期望数一定对不上 ——
#    那是样本写错了，不是扫描器坏了（我第一版就栽在这，期望 3 却得到 6）。
param(
    [string]$Root = '',
    [switch]$SelfTest,
    [string]$SelfTestDir = ''
)

# =====================================================================
# 自检：真样本必须逐条命中，假样本必须一条都不命中。
# 真样本刻意覆盖**每一条判据分支**（关键字名 / 单字母 w,h / 结尾 W,H 的驼峰名 /
# 第 3 参是裸字面量），假样本覆盖「合法 min/max」「RectAt」「表达式末参」「注释里的」。
# ⚠️ 加/改判据时必须同时加真样本，否则「判据写错了」没有任何机器看着。
# =====================================================================
function Invoke-SelfTest([string]$dir) {
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $enc = New-Object System.Text.UTF8Encoding($false)
    $reals = @(
        'Rect{x, y, cardW, 192.0f};',          # 枚举里的名字
        'Rect{x, y, panelW, 40.0f};',
        'Rect{x, y, w, 120.0f};',              # 单字母 —— 原来漏的就是这类
        'Rect{x, y, h, 120.0f};',
        'Rect card{x, y, colW, 76.0f};',       # 驼峰结尾 W
        'Rect{a, b, cellW, 40.0f};',
        'Rect{a, b, iconW, 22.0f};',
        'Rect{a, b, thumbH, 72.0f};',
        'Rect{a, b, 128.0f, 96.0f};'           # 第 3 参是裸字面量（有意保守）
    )
    $fakes = @(
        'Rect{a.min.x, a.min.y, a.max.x, a.max.y};',   # 末参不是裸字面量
        'RectAt(x, y, w, 120.0f);',                    # 不是 Rect{ 构造
        'Rect{x, y, x + cardW, y + 192.0f};',          # 末参是表达式
        'Rect{ImVec2(0, 0), ImVec2(10, 10)};',         # 2 参形式
        '// Rect{x, y, w, 120.0f}  ← 注释里的不许报',   # 注释状态机
        '/* Rect{a, b, colW, 76.0f} */'                # 块注释里的不许报
    )
    [IO.File]::WriteAllBytes((Join-Path $dir 'samples.cpp'), $enc.GetBytes((($reals + $fakes) -join "`n")))
    # ⚠️ 取数必须用与 grep 同一个正则、读它的**捕获组**。第一版写成 `(\d+)$`，
    #    而那一行结尾是 ` ---` ⇒ 锚不上 ⇒ 取到空串 ⇒ [int]'' = 0 ⇒ 自检假失败
    #    （「期望 9 实得 0」看起来像扫描器坏了，实际是自检自己的取数坏了）。
    $got = @(& $PSCommandPath -Root $dir 2>&1 |
                 Select-String -Pattern 'suspected width/height misuse: (\d+)')
    $n = if ($got.Count -gt 0) { [int]$got[0].Matches[0].Groups[1].Value } else { -1 }
    Remove-Item $dir -Recurse -Force
    if ($n -ne $reals.Count) {
        Write-Output "find-rect-wh-misuse 自检 FAILED：期望命中 $($reals.Count)（真样本数），实得 $n"
        exit 1
    }
    Write-Output "find-rect-wh-misuse 自检 PASS（$($reals.Count) 真 + $($fakes.Count) 假样本，命中数正好等于真样本数）"
    exit 0
}
if ($SelfTest) {
    Invoke-SelfTest $(if ($SelfTestDir) { $SelfTestDir } else { Join-Path $env:TEMP 'rectwh-selftest' })
}
$ErrorActionPreference = 'Continue'
$root = if ($Root) { $Root } else { Join-Path $PSScriptRoot '..\src\ui\imgui' }

function Split-TopLevel([string]$s) {
    $parts = @(); $depth = 0; $cur = ''
    foreach ($ch in $s.ToCharArray()) {
        if ($ch -eq '(' -or $ch -eq '[' -or $ch -eq '{') { $depth++ }
        elseif ($ch -eq ')' -or $ch -eq ']' -or $ch -eq '}') { $depth-- }
        if ($ch -eq ',' -and $depth -eq 0) { $parts += $cur; $cur = '' } else { $cur += $ch }
    }
    if ($cur.Trim()) { $parts += $cur }
    return $parts
}

# 同时认两种写法：`Rect{a, b, c, d}`（直接初始化）与 `Rect name{a, b, c, d}`（声明）。
# ⚠️ 早先只认前一种，而本仓两种都有（67 / 97 处）—— 漏掉的恰恰是更主流的那种。
$rxRect = [regex]'\bRect\s*(\w+)?\s*\{'

$hits = 0
Get-ChildItem $root -Recurse -Include *.cpp,*.h | ForEach-Object {
    $file = $_
    $text = [IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8)
    $n = $text.Length
    # ⚠️ 单趟扫描，状态贯通。必须**从 `Rect{` 落点本身**就判它是不是在注释/字符串里：
    #    只在实参内部跟踪状态没用 —— 上一版就是这么漏的，源码里那些解释这个坑的注释
    #    （`// 这里原来写 Rect{x, y, 400.0f, 32.0f}`）实参里本来就没有第二个注释起始符，
    #    内层自然判不出来，于是把注释当命中。一个把自己注释报出来的门禁，
    #    第二次就会被 ignore，工具就废了。
    $i = 0; $inStr = $false; $inLine = $false; $inBlock = $false
    while ($i -lt $n) {
        $c = $text[$i]
        $next = if ($i + 1 -lt $n) { $text[$i + 1] } else { [char]0 }
        if ($inStr) {
            if ($c -eq '"' -and $text[$i - 1] -ne '\') { $inStr = $false }
            $i++; continue
        }
        if ($inLine) {
            if ($c -eq "`n") { $inLine = $false }
            $i++; continue
        }
        if ($inBlock) {
            if ($c -eq '*' -and $next -eq '/') { $inBlock = $false; $i += 2; continue }
            $i++; continue
        }
        if ($c -eq '"') { $inStr = $true; $i++; continue }
        if ($c -eq '/' -and $next -eq '/') { $inLine = $true; $i += 2; continue }
        if ($c -eq '/' -and $next -eq '*') { $inBlock = $true; $i += 2; continue }
        if ($c -eq 'R') {
            $m = $rxRect.Match($text, $i)
            if (-not $m.Success -or $m.Index -ne $i) { $i++; continue }
            $open = $m.Index + $m.Length - 1   # '{' 的位置
            # 花括号配对，从 '{' 开始往后找匹配的 '}'（仍要跳过字符串与注释）。
            $depth = 0; $j = $open; $sStr = $false; $sLine = $false; $sBlock = $false
            for (; $j -lt $n; $j++) {
                $d = $text[$j]
                $dNext = if ($j + 1 -lt $n) { $text[$j + 1] } else { [char]0 }
                if ($sStr) {
                    if ($d -eq '"' -and $text[$j - 1] -ne '\') { $sStr = $false }
                    continue
                }
                if ($sLine) { if ($d -eq "`n") { $sLine = $false }; continue }
                if ($sBlock) { if ($d -eq '*' -and $dNext -eq '/') { $sBlock = $false; $j++ }; continue }
                if ($d -eq '"') { $sStr = $true; continue }
                if ($d -eq '/' -and $dNext -eq '/') { $sLine = $true; $j++; continue }
                if ($d -eq '/' -and $dNext -eq '*') { $sBlock = $true; $j++; continue }
                if ($d -eq '{') { $depth++ } elseif ($d -eq '}') { $depth--; if ($depth -eq 0) { break } }
            }
            if ($j -lt $n) {
                $body = $text.Substring($open + 1, $j - $open - 1)
                $args = Split-TopLevel $body
                if ($args.Count -eq 4) {
                    $a3 = $args[2].Trim(); $a4 = $args[3].Trim()
                    # 第 3 参「尺寸样」= 标识符**以 W/w/H/h 结尾**（`^\w*[wh]$`，单字母
                    # 的 `w`/`h` 也算）或裸浮点字面量。按结尾字符判，不按名字枚举。
                    $sizeLike = ($a3 -match '(?i)(^\w*[wh]$|\d+(\.\d+)?f?$)') -and
                                ($a4 -match '^\d+(\.\d+)?[fF]?$')
                    if ($sizeLike) {
                        $line = ($text.Substring(0, $i) -split "`n").Count
                        $hits++
                        "{0}:{1}`n    Rect{{{2}}}`n    a3='{3}'  a4='{4}'" -f $file.Name, $line, ($body -replace '\s+', ' '), $a3, $a4
                    }
                }
            }
            $i = $j + 1; continue
        }
        $i++
    }
}
"--- suspected width/height misuse: $hits ---"
