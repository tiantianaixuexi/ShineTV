# 找「按容器高度静默截断列表」的写法。
#
# 形如：
#     for (const auto& row : rows) {
#         if (y + 24.0f > body.max.y) {
#             break;          // ← 列表尾部被丢弃，界面上**没有任何提示**
#         }
#         ...
#     }
#
# 为什么它是缺陷：自绘列表不像 DOM 那样自动给滚动条。`break` 之后用户看到的是一个
# 「就这么多」的列表，**看不出还有第 N+1 条**。实测：出图页「绑定」页签里 Comfy 队列
# 有 50 个任务时只画出前 6 个，剩下的无声消失 —— 界面在**骗人**，不是缺功能。
#
# 同一族的另外两种写法也一并报（它们等价地「砍行」）：
#   - `if (y + N > body.max.y) { break; }`          ← 逐条判断后 break
#   - `if (rowY + N > body.max.y) { break; }`       ← 同上，行号预先算好
#   - `int maxRows = <由 body.height() 反推>; shown = min(total, maxRows);`  ← 反推式砍行
#
# 判据（保守，宁可多报）：
#   - 找到 `if (<标识符> + <数字> > <标识符>.max.y)` 且其后 2 行内出现 `break;`
#   - 找到 `maxRows` / `shown` 之类由 `.height()` / `max.y` 反推的计数上限
#
# 命中只说明「疑似」：如果后面紧跟一句「共 N 条，显示前 M 条」之类的脚注，那是
# **已诚实标注**的降级，可以放过。脚本不判断这一点，请人眼看一眼。
#
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File tools\find-silent-truncation.ps1
#       powershell ... -File tools\find-silent-truncation.ps1 -Root <目录>
# -Root 只为**自检**存在：改扫描逻辑后必须证明它还抓得到真样本（临时目录跑，
# 期望命中数 = 真样本数）。一个「改完报 0」的扫描器和一个坏掉的没区别。
param(
    [string]$Root = ''
)
$ErrorActionPreference = 'Continue'
$root = if ($Root) { $Root } else { Join-Path $PSScriptRoot '..\src\ui\imgui' }

# ① 逐条 break：if (y + 24.0f > body.max.y) {  break; }
$rxBreak = [regex]'(?m)^[ \t]*if\s*\(\s*([A-Za-z_]\w*)\s*\+\s*[\d.]+f?\s*>\s*[A-Za-z_]\w*\.max\.y\s*\)'

# ② 反推式砍行：int maxRows = ...(body.height() ...)/... 或 int shown = min(..., maxRows)
$rxReverse = [regex]'(?m)^[ \t]*(?:const\s+)?int\s+(\w*(?:maxRows|shown|visibleRows|drawRows)\w*)\s*='

$hits = @()
$suppressed = @()
Get-ChildItem $root -Recurse -Include *.cpp,*.h | ForEach-Object {
    $file = $_
    $text = [IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8)
    $lines = $text -split "`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        $trim = $line.TrimStart()
        # 跳过注释行
        if ($trim.StartsWith('//') -or $trim.StartsWith('*') -or $trim.StartsWith('/*')) { continue }

        $kind = $null
        if ($rxBreak.IsMatch($line)) {
            # 往后 3 行内找 break;
            $found = $false
            for ($k = $i + 1; $k -le [Math]::Min($i + 3, $lines.Count - 1); $k++) {
                if ($lines[$k] -match '\bbreak\s*;') { $found = $true; break }
            }
            if ($found) { $kind = 'break-on-overflow' }
        }
        elseif ($rxReverse.IsMatch($line) -and ($line -match '\.height\(\)|\.max\.y')) {
            $kind = 'reverse-derived-row-cap'
        }
        if (-not $kind) { continue }

        # 显式豁免：命中行**往上 6 行**内出现 `scan:allow-silent-truncation` 且后面写了理由。
        # 豁免**必须带理由** —— 一个没有理由的豁免等于把门禁关掉。
        # 豁免项照样打印（单列一节），不静默：被豁免的位置要让后来人看得见。
        $suppress = $null
        for ($k = [Math]::Max(0, $i - 6); $k -lt $i; $k++) {
            $m = [regex]::Match($lines[$k], 'scan:allow-silent-truncation\s*(.*)$')
            if ($m.Success) {
                $reason = $m.Groups[1].Value.Trim().TrimEnd('*/').Trim()
                if ($reason) { $suppress = $reason } else { $suppress = '(未写理由 —— 豁免无效)' }
                break
            }
        }
        $entry = [pscustomobject]@{
            File = $file.Name
            Line = $i + 1
            Kind = $kind
            Text = $line.Trim()
            Suppress = $suppress
        }
        if ($suppress -and $suppress -notlike '(*') { $suppressed += $entry } else { $hits += $entry }
    }
}

Write-Output "--- suspected silent truncation: $($hits.Count) ---"
foreach ($h in $hits) {
    Write-Output "$($h.File):$($h.Line)  [$($h.Kind)]"
    Write-Output "    $($h.Text)"
}
if ($suppressed.Count -gt 0) {
    Write-Output ''
    Write-Output "--- acknowledged (not counted) : $($suppressed.Count) ---"
    foreach ($h in $suppressed) {
        Write-Output "$($h.File):$($h.Line)  [$($h.Kind)]  理由: $($h.Suppress)"
    }
}
if ($hits.Count -gt 0) {
    Write-Output ''
    Write-Output '每一条都要人眼确认：后面若已写明「共 N 条，显示前 M 条」这类脚注，可放过。'
    Write-Output '没写的：条目静默丢失 = 界面在骗人。改成 ScrollRegion 让尾部可达，或加脚注。'
    Write-Output '确实要保留的，在命中行往上 6 行内写：// scan:allow-silent-truncation <理由>'
}
