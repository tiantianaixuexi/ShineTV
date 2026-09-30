<#
.SYNOPSIS
    抓 ImDrawList 的「颜色 / 圆角参数错位」。

.DESCRIPTION
    ImGui 的绘制原语签名是 (p_min, p_max, col, rounding, ...) —— **颜色在圆角前面**：

        void AddRectFilled(const ImVec2& p_min, const ImVec2& p_max, ImU32 col,
                           float rounding = 0.0f, ImDrawFlags flags = 0);

    写成 (p_min, p_max, rounding, col) **编译不报错、不崩、当帧也照常出图**，
    后果是：

      * col 位收到一个半径（比如 14），于是颜色 = 0x0000000E，全透明黑 —— 画了等于没画；
      * rounding 位收到一个颜色（0xFF000000），被截成巨大的圆角半径；
      * 半径正好是 0 的那次最彻底：col 恰好为 0，被 `if (col == 0) return;` 整块丢掉。

    三条症状都表现为「函数明明调用了、同一帧别的图元也画出来了，但屏幕上什么都没有」。
    **编译期完全沉默，只有读渲染结果才发现** —— 本仓就实际栽过：
    Draw.cpp 的 DrawShadow 两处都写反，28 个接档点一个投影都没画出来，
    而 80 张取证图全部 saved、overall=PASS、四条门禁全绿。

    所以这里按「静态可疑」扫：**AddRectFilled 的第 3 个实参不像颜色表达式就报出来**。
    判定为可疑而不是违规：颜色可以是任意表达式，白名单只能覆盖常见的几种。

    同族的 AddRect / AddRectFilledMultiColor / PathRect 参数顺序一致，一并扫。

.PARAMETER Root
    扫描目录。默认 src/ui/imgui。

.PARAMETER SelfTest
    内建样本自检：确认「能报出真的、放过假的」。

.EXAMPLE
    powershell -File tools\find-draw-arg-order.ps1
    powershell -File tools\find-draw-arg-order.ps1 -Root <样本目录>
    powershell -File tools\find-draw-arg-order.ps1 -SelfTest
#>
[CmdletBinding()]
param(
    [string]$Root,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
if (-not $Root) { $Root = Join-Path (Split-Path -Parent $PSScriptRoot) 'src\ui\imgui' }

# 颜色位置的「像颜色」特征。
# 刻意列得宽：本工具是**人工复核**工具，报出来给人看，误报只是多看一眼；
# 漏报才是危险（真错就在代码里却没人知道）。所以宁可多认几个名字。
$ColorHints = @(
    'Color', 'IM_COL32', 'ImVec4', 'ImU32', 'ToImU32', 'WithAlpha', 'WithShadowAlpha',
    'LerpColor', 'Mix', 'Tint', 'Fade', 'Shade', 'GlassColor', 'ToneColor',
    'Alpha', 'RGBA', 'Glow', 'Dim', 'Scrim',
    'color', 'col', 'Col', 'fill', 'Fill', 'border', 'Border', 'Line',
    'Text', 'Accent', 'Panel', 'Overlay', 'Surface', 'bg', 'Bg', 'fg', 'Fg'
)

function Get-Calls {
    param([string]$Text)
    $out = @()
    # 先把注释整段挖掉，免得注释里的 AddRectFilled( 混进来当调用点。
    $clean = [regex]::Replace($Text, '(?s)/\*.*?\*/', ' ')
    $clean = [regex]::Replace($clean, '(?m)//.*$', ' ')
    $names = @('AddRectFilledMultiColor', 'AddRectFilled', 'AddRect', 'PathRect')
    foreach ($fn in $names) {
        $needle = $fn + '('
        $from = 0
        while ($true) {
            $idx = $clean.IndexOf($needle, $from)
            if ($idx -lt 0) { break }
            $from = $idx + $needle.Length
            # 从 '(' 的**下一位**开始扫：深度 0 = 还没进任何一层。
            # ⚠️ 从行首扫是错的 —— '(' 之前的括号会先把深度顶起来，后面再也回不到 0，
            #    实参就再也切不开（$args 变成一整坨，Count<3 直接被放过）。
            $depth = 0
            $buf = ''
            $end = $clean.Length
            for ($k = $idx + $needle.Length; $k -lt $clean.Length; $k++) {
                $ch = $clean[$k]
                if ($ch -eq '(') {
                    $depth++
                    $buf += $ch
                }
                elseif ($ch -eq ')') {
                    if ($depth -eq 0) { $end = $k; break }
                    $depth--
                    $buf += $ch
                }
                else { $buf += $ch }
            }
            $lineNo = 1 + ([regex]::Matches($clean.Substring(0, [Math]::Min($idx, $clean.Length)), "`n")).Count
            $args = @()
            $cur = ''
            $d2 = 0
            foreach ($ch in $buf.ToCharArray()) {
                if ($ch -eq '(' -or $ch -eq '[') { $d2++ }
                elseif ($ch -eq ')' -or $ch -eq ']') { $d2-- }
                if ($ch -eq ',' -and $d2 -eq 0) { $args += $cur; $cur = '' } else { $cur += $ch }
            }
            if ($cur.Trim()) { $args += $cur }
            $out += [pscustomobject]@{ Fn = $fn; Line = $lineNo; Args = $args }
        }
    }
    return $out
}

function Test-Suspicious {
    param($Call)
    # MultiColor 的第 3~6 个实参都是颜色，不用查。
    if ($Call.Fn -eq 'AddRectFilledMultiColor') { return $null }
    # 少于 3 个实参 = 没给颜色，一定有问题，但不是本脚本的职责。
    if ($Call.Args.Count -lt 3) { return $null }
    $third = $Call.Args[2].Trim()
    # 判据就一条：**第 3 个实参里找不到任何颜色特征**。
    #
    # ⚠️ 曾经还想用「像不像几何表达式」的正则来提高置信度，结果**只制造漏报**：
    #    真正咬人的那行是 `radius + grow`（带加号、带第二个标识符），三条正则
    #    全都不匹配，于是放过去了 —— 而单行的 `radius` 反而被抓到。工具漏报自己
    #    该抓的东西，比误报危险得多，所以判据只留「有没有颜色特征」这一条。
    foreach ($h in $ColorHints) {
        if ($third -like "*$h*") { return $null }
    }
    return "[$($Call.Fn)] 第 3 个实参里没有颜色特征: $third"
}

if ($SelfTest) {
    $cases = @(
        @{ n = 'wrong-order.cpp';      f = "draw->AddRectFilled(a, b, 10.0f, ColorPanel());"; want = 1 },
        @{ n = 'wrong-order-ident.cpp'; f = "draw->AddRectFilled(a, b, radius, col);"; want = 1 },
        # 下面这条是**本仓真实踩过的那一行**（Draw.cpp 的 DrawShadow，跨行 + 算式）。
        # 早期版本靠「像不像几何表达式」的正则判定，结果单行的 radius 抓到了、
        # 这条带加号的漏掉了 —— 扫描器漏报自己该抓的东西，比误报危险得多。
        @{ n = 'wrong-order-expr.cpp'; f = "draw->AddRectFilled(a - ImVec2(g, g), b + ImVec2(g, g),`r`n                    radius + grow, WithShadowAlpha(base, stepAlpha));"; want = 1 },
        @{ n = 'right-order.cpp';      f = "draw->AddRectFilled(a, b, ColorPanel(), 10.0f);"; want = 0 },
        @{ n = 'three-args.cpp';       f = "draw->AddRectFilled(a, b, ColorScrim());"; want = 0 },
        @{ n = 'multicolor.cpp';       f = "draw->AddRectFilledMultiColor(a, b, c1, c2, c3, c4);"; want = 0 },
        @{ n = 'commented.cpp';        f = "// draw->AddRectFilled(a, b, 10.0f, ColorPanel());"; want = 0 },
        @{ n = 'blockcomment.cpp';     f = "/* draw->AddRectFilled(a, b, 10.0f, ColorPanel()); */"; want = 0 },
        @{ n = 'right-order-withalpha.cpp'; f = "draw->AddRectFilled(a, b, WithAlpha(c, t), radius);"; want = 0 },
        @{ n = 'right-order-toimug32.cpp'; f = "draw->AddRectFilled(a, b, theme::ToImU32(v), radius);"; want = 0 }
    )
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('argorder-' + [System.Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    $fail = 0
    foreach ($c in $cases) {
        [System.IO.File]::WriteAllText((Join-Path $tmp $c.n), $c.f + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
    }
    foreach ($c in $cases) {
        $text = [System.IO.File]::ReadAllText((Join-Path $tmp $c.n))
        $got = 0
        foreach ($call in (Get-Calls $text)) { if (Test-Suspicious $call) { $got++ } }
        $ok = ($got -eq $c.want)
        if (-not $ok) { $fail++ }
        '{0,-26} 期望{1}  实得{2}  {3}' -f $c.n, $c.want, $got, $(if ($ok) { 'OK' } else { 'BAD' })
    }
    Remove-Item -Recurse -Force $tmp
    if ($fail -eq 0) {
        "find-draw-arg-order 自检 PASS（$($cases.Count) 份样本）"
        exit 0
    }
    "find-draw-arg-order 自检 FAIL（$fail 份样本不符）"
    exit 1
}

if (-not (Test-Path $Root)) { "find-draw-arg-order: 目录不存在 $Root"; exit 1 }
$hits = @()
$files = Get-ChildItem -Path $Root -Recurse -Include *.cpp, *.h -File
foreach ($f in $files) {
    $text = [System.IO.File]::ReadAllText($f.FullName)
    foreach ($call in (Get-Calls $text)) {
        $s = Test-Suspicious $call
        if ($s) { $hits += [pscustomobject]@{ File = $f.FullName; Line = $call.Line; Msg = $s } }
    }
}
"--- 扫描 $Root : $($files.Count) 个文件 ---"
if ($hits.Count -eq 0) {
    "find-draw-arg-order: 未发现颜色/圆角参数错位"
    exit 0
}
foreach ($h in $hits) {
    $rel = $h.File.Substring($Root.Length).TrimStart('\')
    "$rel`:$($h.Line)  $($h.Msg)"
}
"--- 疑似参数错位: $($hits.Count) ---"
