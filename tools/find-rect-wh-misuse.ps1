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
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File tools\find-rect-wh-misuse.ps1
$ErrorActionPreference = 'Continue'
$root = Join-Path $PSScriptRoot '..\src\ui\imgui'

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

$hits = 0
Get-ChildItem $root -Recurse -Include *.cpp,*.h | ForEach-Object {
    $file = $_
    $text = [IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8)
    for ($i = 0; $i -lt $text.Length; $i++) {
        if ($text[$i] -ne 'R') { continue }
        if ($text.Substring($i, 5) -ne 'Rect{') { continue }
        # 花括号配对（跳过字符串字面量里的花括号，这里只求覆盖住真实调用）
        $depth = 0; $j = $i + 4; $inStr = $false
        for (; $j -lt $text.Length; $j++) {
            $c = $text[$j]
            if ($c -eq '"' -and $text[$j - 1] -ne '\') { $inStr = -not $inStr; continue }
            if ($inStr) { continue }
            if ($c -eq '{') { $depth++ } elseif ($c -eq '}') { $depth--; if ($depth -eq 0) { break } }
        }
        if ($j -ge $text.Length) { continue }
        $body = $text.Substring($i + 5, $j - $i - 5)
        $args = Split-TopLevel $body
        if ($args.Count -ne 4) { continue }
        $a3 = $args[2].Trim(); $a4 = $args[3].Trim()
        $sizeLike = ($a3 -match '(?i)(width|height|cardW|panelW|boxW|\d+(\.\d+)?f?$)') -and
                    ($a4 -match '^\d+(\.\d+)?[fF]?$')
        if (-not $sizeLike) { continue }
        $line = ($text.Substring(0, $i) -split "`n").Count
        $hits++
        "{0}:{1}`n    Rect{{{2}}}`n    a3='{3}'  a4='{4}'" -f $file.Name, $line, ($body -replace '\s+', ' '), $a3, $a4
    }
}
"--- suspected width/height misuse: $hits ---"
