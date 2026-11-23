# 门禁：手写列表步进与手算滚动内容高度（refactor/component-reuse §7.3 的 R7 第三条）。
#
# ---- 守的两条规则 ----
#   1. `y += A + B`（**两个以上加项**的纵向推进）—— `kit::Stack` / `kit::ScrollList`
#      已有组件，`y += 元素高 + 间距` 是它们收掉的手写形态。
#      ⚠️ **单项**推进不报：`y += 22.0f;`（裸行距）是 R4 明文**刻意不收**的形态
#      （进度.md 第十四轮「刻意不收的两处」），判据与那条口径一致 —— 只抓「高 + 间距」
#      这种把元素高与间距**手算在一起**的写法（单项的语义就是「裸行距」，谁也不会把它
#      误读成某个元素的高度）。
#   2. `setContentHeight(<含 * 或 + 的表达式>)` —— 滚动内容高度该由
#      `ScrollList` 自动上报（rowCount × pitch）或由 `Tree` / `Stack` 的**返回值 / 游标**
#      给出（`Cursor() - top` 这种**减法**形态是规范本身，不报）。
#      乘法（`rowCount * 22.0f`）= 把 ScrollList 的自动内容高度手抄了一遍；
#      加法 = 把若干段高度手加起来，段一变就漂。
#
# ---- 扫描范围：只扫 src/ui/imgui/pages/ ----
# kit/ 是 ScrollList / Stack 的实现地（内部合法地做这些算术），verify/ 是取证层。
#
# ---- 存量怎么办：棘轮基线（ratchet），与前两把扫描器同一套 ----
# `tools\find-manual-list.baseline.txt` 按**文件**记存量数。新增 > 基线 ⇒ exit 1；
# 收编后 -WriteBaseline 收紧，只降不升。
#
# ---- 用法 ----
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\find-manual-list.ps1
#   powershell ... -File tools\find-manual-list.ps1 -SelfTest
#   powershell ... -File tools\find-manual-list.ps1 -Root <目录>
#   powershell ... -File tools\find-manual-list.ps1 -WriteBaseline
param(
    [string]$Root = '',
    [switch]$SelfTest,
    [string]$SelfTestDir = '',
    [switch]$WriteBaseline
)

# R1：y += <含加号的推进>。「+=」右端出现 '+' 才算（单项 = 裸行距，放行）。
# ⚠️ 右端**含括号的一律不抓**：`y += DrawJumpButtons(Rect{…, y + 84.8f}, draw)` 的 '+'
#    在构造实参里、`y += std::max(A + B, C + D)` 是两列取高者的**布局语义**
#    （进度.md 第二十轮：这两类都实测命中过，全是误报）。含函数调用的形态也一并不抓
#    —— 那是 Stack.Next 的辖区，正则分不清调用边界，宁可漏不要冤。
$rxYAdvance = [regex]'\by\s*\+=\s*[^;()+\r\n]+(?:\+[^;()+\r\n]+)+\s*;'
# R2：setContentHeight(<含 * 或 + 的表达式>)。减法形态（游标 - 顶）是规范，不抓。
$rxContentH = [regex]'\.setContentHeight\(\s*[^);]*[+*][^);]*\)'

# 预处理与前两把扫描器逐字相同（三把门禁共用同一套剥注释 / 掏字符串的口径）。
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
        $matched = @()
        if ($rxYAdvance.IsMatch($code)) { $matched += 'manual-y-advance' }
        if ($rxContentH.IsMatch($code)) { $matched += 'manual-content-height' }
        foreach ($kind in $matched) {
            # 显式豁免：命中行**往上 6 行**内出现 `scan:allow-manual-list` 且后面写了理由
            # （与 find-silent-truncation.ps1 同一套机制：豁免必须带理由、照样打印不静默）。
            $suppress = $null
            for ($k = [Math]::Max(0, $i - 6); $k -lt $i; $k++) {
                $m = [regex]::Match($lines[$k], 'scan:allow-manual-list\s*(.*)$')
                if ($m.Success) {
                    $reason = $m.Groups[1].Value.Trim().TrimEnd('*/').Trim()
                    if ($reason) { $suppress = $reason } else { $suppress = '(未写理由 —— 豁免无效)' }
                }
            }
            $hits += [pscustomobject]@{ Line = $i + 1; Kind = $kind; Text = $code.Trim(); Suppress = $suppress }
        }
    }
    return $hits
}

# =====================================================================
# 自检：真样本覆盖两条规则 ×（字面量+字面量 / 变量+常量 / 成员调用+字面量 /
# 乘法 / 加乘混合）；假样本覆盖单项裸行距（刻意不收）、x 轴推进、减法游标、
# 单实参、三种注释形态、字符串字面量、yy 同形不命中。
# 期望数按判据现推（见 $expect 的推导注释）。
# =====================================================================
if ($SelfTest) {
    $dir = if ($SelfTestDir) { $SelfTestDir } else { Join-Path $env:TEMP 'manuallist-selftest' }
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $enc = New-Object System.Text.UTF8Encoding($false)

    $reals = @(
        'y += 132.0f + 8.0f;',                                          # R1：字面量+字面量
        'y += cardH + kGap;',                                           # R1：变量+常量
        'y += h1 + h2;',                                                # R1：变量+变量
        'treeScroll.setContentHeight(rowCount * 22.0f);',               # R2：乘法
        'scroll.setContentHeight(a + b);',                              # R2：加法
        'sr.setContentHeight(18.0f + 22.0f * n + tailH);'               # R2：加乘混合（一条）
    )
    $fakes = @(
        'y += 22.0f;',                                                  # 单项 = 裸行距，刻意不收
        'x += a + b;',                                                  # 不是 y
        'ty += 15.0f;',                                                 # 单项
        'scroll.setContentHeight(h);',                                  # 单实参
        'scroll.setContentHeight(side.Cur() - inner.min.y);',           # 减法游标 = 规范
        '// y += a + b; 行注释不许报',
        '/* y += a + b; 块注释不许报 */',
        'const char* k = "y += a + b;";',                               # 字符串字面量
        'yy += a + b;',                                                 # \by 不中 yy
        'y += DrawJumpButtons(Rect{x, y, w, y + 84.8f}, draw);',        # '+' 在构造实参里（实测误报）
        'y += std::max(220.0f + kA, 164.0f + kB);'                      # 两列取高者 = 布局语义（实测误报）
    )
    [IO.File]::WriteAllBytes((Join-Path $dir 'samples.cpp'), $enc.GetBytes((($reals + $fakes) -join "`n")))

    $got = @(& $PSCommandPath -Root $dir 2>&1 |
             Select-String -Pattern 'manual-list hits: (\d+)')
    $n = if ($got.Count -gt 0) { [int]$got[0].Matches[0].Groups[1].Value } else { -1 }
    Remove-Item $dir -Recurse -Force
    # 期望数按判据现推：真样本 6 行逐条命中 = 6（含括号的 R1 形态不在面上，见判据注释）；
    # 假样本 11 条一条不中。
    $expect = 6
    if ($n -ne $expect) {
        Write-Output "find-manual-list 自检 FAILED：期望命中 $expect，实得 $n"
        exit 1
    }
    Write-Output "find-manual-list 自检 PASS（6 真 + 11 假样本，命中数 $n = 期望 $expect）"
    exit 0
}

# =====================================================================
# 正常路径（与 find-raw-text.ps1 同构：逐文件扫 → 棘轮基线）
# =====================================================================
$ErrorActionPreference = 'Continue'
$pagesRoot = if ($Root) { $Root } else { Join-Path $PSScriptRoot '..\src\ui\imgui\pages' }
if (-not (Test-Path $pagesRoot)) {
    Write-Output "find-manual-list: 找不到 $pagesRoot"
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
        $active = @($hits | Where-Object { -not $_.Suppress })
        foreach ($h in $hits) { $all += [pscustomobject]@{ File = $rel; Line = $h.Line; Kind = $h.Kind; Text = $h.Text; Suppress = $h.Suppress } }
        if ($active.Count -gt 0) { $perFile[$rel] = $active.Count }
    }
}

$baselinePath = Join-Path $PSScriptRoot 'find-manual-list.baseline.txt'
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
    $lines = @('# find-manual-list 棘轮基线：文件=存量条数。收编一个文件就跑 -WriteBaseline 收紧，只降不升。')
    foreach ($k in ($perFile.Keys | Sort-Object)) { $lines += "$k=$($perFile[$k])" }
    [IO.File]::WriteAllLines($baselinePath, $lines, (New-Object System.Text.UTF8Encoding($true)))
    Write-Output "find-manual-list: 基线已写入 $baselinePath（$($perFile.Count) 个文件，共 $(($perFile.Values | Measure-Object -Sum).Sum) 条）"
    exit 0
}

$suppressed = @($all | Where-Object { $_.Suppress })
$active = @($all | Where-Object { -not $_.Suppress })
foreach ($h in $all) {
    "{0}:{1}  [{2}]" -f $h.File, $h.Line, $h.Kind
    "    $($h.Text)"
}
''
"--- manual-list hits: $($active.Count) ---"
if ($suppressed.Count -gt 0) {
    Write-Output "--- acknowledged (not counted) : $($suppressed.Count) ---"
    foreach ($h in $suppressed) {
        Write-Output "$($h.File):$($h.Line)  [$($h.Kind)]  理由: $($h.Suppress)"
    }
}

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
    Write-Output '列表推进走 kit::Stack / ScrollList：y += 元素高+间距 与手算 setContentHeight 都是漏网形态（单项裸行距刻意不收，见进度.md 第十四轮）。'
    exit 1
}
if ($active.Count -gt 0) {
    Write-Output "find-manual-list: PASS（$($active.Count) 条存量全部在基线内；收编后跑 -WriteBaseline 收紧棘轮）"
} else {
    Write-Output 'find-manual-list: PASS（0 处手写步进 / 手算内容高度）'
}
exit 0
