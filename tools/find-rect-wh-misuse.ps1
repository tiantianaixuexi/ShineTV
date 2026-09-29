# 找「把宽高写进 kit::Rect 四参构造」的误用。
#
# kit::Rect(float,float,float,float) 是 (minX, minY, maxX, maxY)，不是 (x, y, w, h)。
# 写错不报编译错：max < min 时 DrawRoundRect / HitTest 直接 return，控件整个不画也不可点。
#
# 判据（保守，宁可多报）：取出每个 `Rect{...}` 的**顶层**四个实参，
#   - 恰好 4 个顶层实参（2 参与 ImVec2 形式不在此列）
#   - 第 3 个实参是「尺寸样」：变量名以 W/w/H/h 结尾、含 Width/Height/cardW，或裸浮点字面量
#   - 第 4 个实参是裸浮点字面量
# 命中只说明「疑似」，要人眼看一眼：第 3/4 参若是 x+w / y+h 这种就是合法的 min/max。
#
# ⚠️ **纯字面量的合法 min/max 会被报出来，这是有意的保守取舍。**
#    `Rect{0.0f, 0.0f, 96.0f, 32.0f}` 是完全合法的 (minX,minY,maxX,maxY)，但它同时
#    满足上面两条判据（第 3 参是裸浮点、第 4 参是裸浮点）—— 语法层面无法与
#    `Rect{x, y, cardW, 192.0f}` 区分。宁可多报也不漏报（漏报的后果是控件整张不画）。
#    真实树报 0 只是因为那里合法的 min/max 第 4 参几乎都不是裸字面量。
#
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File tools\find-rect-wh-misuse.ps1
#       powershell ... -File tools\find-rect-wh-misuse.ps1 -Root <目录>
# -Root 只为**自检**存在：改扫描逻辑后必须证明它还抓得到真误用（用一份含真/假样本的
# 临时目录跑，期望命中数 = 真样本数）。一个「改完报 0」的扫描器和一个坏掉的没区别。
# ⚠️ 自检样本里的「合法样本」必须用**末参非裸字面量**的形态（`Rect{a.min.x,…,a.max.y}`）
#    或走 `RectAt(...)`。拿纯字面量 min/max 当合法样本，期望数一定对不上 ——
#    那是样本写错了，不是扫描器坏了（我第一版就栽在这，期望 3 却得到 6）。
param(
    [string]$Root = ''
)
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
                    $sizeLike = ($a3 -match '(?i)(width|height|cardW|panelW|boxW|\d+(\.\d+)?f?$)') -and
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
