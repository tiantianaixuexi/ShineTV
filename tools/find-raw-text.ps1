# 门禁：页面层禁止裸文字绘制与字面量字号（refactor/记忆.md「页面层禁止清单」第 1/2 条）。
#
# ---- 守的两条规则 ----
#   1. 裸 `AddText(`          —— 一律 `kit::Text`（TextStyle：token + 色 + 字面 + 折行）。
#      页面层任何 `->AddText(` / `.AddText(` 都算裸调用；共享原语（DrawTextClipped /
#      LabelWidth / CenterTextY …）不算 —— 它们收编在 kit 与 PageCommon，是规范本身。
#   2. `FontAt(11.5f)` 字面量字号 —— 一律 `theme::font::*` token。
#      三个取档助手同族同判：`FontAt` / `FontBoldAt` / `MonoAt`（kit/Fonts.h:66-73）。
#      实参是标识符（token 或变量）不报 —— 判据只抓**数字字面量**，与禁止清单措辞一致。
#
# ---- 扫描范围：只扫 src/ui/imgui/pages/ ----
# 禁止清单是「页面层」的纪律：kit/ 是 `kit::Text` 的实现地（它内部合法地调 AddText），
# theme/ 是 token 源头，verify/ 是取证层 —— 都不在纪律面上。这与 check-colors.ps1
# 只扫 src/ui/imgui 的理由同构：规则只对绘制调用点成立，而页面层就是调用点所在。
# 代价：在 pages/ 之外新建绘制文件会逃过本门禁 —— 由 check-layers.ps1 的分层规则兜底。
#
# ---- 存量怎么办：棘轮基线（ratchet），不是豁免 ----
# 本门禁上前时页面层还有约 180 处存量（refactor/README.md：81 处裸 AddText / 208 处
# 硬编码字号，FontAt 字面量是其中在 pages 的部分）。逐条写豁免理由（check-colors 的
# theme-ok 路数）等于把每个待收编点洗白成永久合法 —— 不行。
# 所以：基线文件 `tools\find-raw-text.baseline.txt` 按**文件**记存量数
# （`相对路径=条数`，# 开头是注释）。判据：
#   - 某文件命中数 > 基线 ⇒ **新增违规**，exit 1；
#   - 文件不在基线里 ⇒ 基线按 0 ⇒ 新文件一上来就得干净；
#   - 收编变少后跑 `-WriteBaseline` 把基线收到当前值（**只降不升**是纪律，脚本不替你升：
#     写基线时若发现某文件现值比基线大，仍照写 —— 但那轮门禁本来就是红的，写完照红）。
# 棘轮有「已收编但基线没收紧」的空窗（20 收到 5、基线还是 20，再添 10 不报）——
# 所以收编一个文件就立刻 -WriteBaseline，别攒。
#
# ---- 用法 ----
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\find-raw-text.ps1
#   powershell ... -File tools\find-raw-text.ps1 -SelfTest
#   powershell ... -File tools\find-raw-text.ps1 -Root <目录>          （自检/定点扫描）
#   powershell ... -File tools\find-raw-text.ps1 -WriteBaseline        （收编后收紧基线）
#
# ⚠️ -SelfTest 与 -Root 是改判据后的必跑项：真样本必须逐条命中、假样本一条不命中。
#    「改完报 0」的扫描器和一个坏掉的没区别 —— check-colors / find-rect-wh-misuse 同款教训。
param(
    [string]$Root = '',
    [switch]$SelfTest,
    [string]$SelfTestDir = '',
    [switch]$WriteBaseline
)

# 实参为数字字面量（含半点 11.5f / 负号 / 前导点），token 与变量不命中。
$rxFontLiteral = [regex]'\b(?:FontAt|FontBoldAt|MonoAt)\s*\(\s*[-+]?(?:\d+(?:\.\d+)?|\.\d+)f?\s*\)'
$rxAddText     = [regex]'\bAddText\s*\('

# 剥掉行尾注释（字符串外的 // 才算，"https://…" 不算）—— 与 check-colors.ps1 同款。
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

# 把字符串字面量内容掏空（引号保留、位置不变）：页面层不该出现 "FontAt(11.5f)" 这样的
# 代码，但取证注释/日志模板里出现过 `AddText(` 字样的可能性不为零 —— 掏空比事后争论省事。
function Remove-StringLiterals([string]$line) {
    $sb = New-Object System.Text.StringBuilder $line.Length
    $inStr = $false; $inChr = $false; $esc = $false
    foreach ($c in $line.ToCharArray()) {
        if ($esc) { [void]$sb.Append(' '); $esc = $false; continue }
        if ($c -eq '\') { [void]$sb.Append($c); $esc = $true; continue }
        if ($c -eq '"' -and -not $inChr) { $inStr = -not $inStr; [void]$sb.Append($c); continue }
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
        if ($rxAddText.IsMatch($code))     { $hits += [pscustomobject]@{ Line = $i + 1; Kind = 'raw-AddText';      Text = $code.Trim() } }
        if ($rxFontLiteral.IsMatch($code)) { $hits += [pscustomobject]@{ Line = $i + 1; Kind = 'font-size-literal'; Text = $code.Trim() } }
    }
    return $hits
}

# =====================================================================
# 自检：真样本覆盖两条规则 ×（裸 AddText 的 ->/. 前缀、三个取档助手、半点/整数/
# 负号、一行两规齐中）；假样本覆盖 token 实参、变量实参、共享原语、注释三种形态、
# 字符串字面量、kit 外形相似物。期望数**现算**（$reals.Count），不写死 —— 写死的数字
# 一改就漂，check-colors 自检注释里那条「写着 11 实际早就不是」就是前车。
# =====================================================================
if ($SelfTest) {
    $dir = if ($SelfTestDir) { $SelfTestDir } else { Join-Path $env:TEMP 'rawtext-selftest' }
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $enc = New-Object System.Text.UTF8Encoding($false)

    $reals = @(
        'draw->AddText(font, 11.5f, pos, col, "x");',          # 裸 AddText（-> 前缀）
        'cdraw.AddText(f, s, p, c, t);',                       # 裸 AddText（. 前缀）
        'draw->AddText(FontBoldAt(12.5f), 12.5f, a, b, c);',   # 一行两规齐中（计 2）
        'FontAt(11.0f);',                                      # 字面量（整数）
        'FontBoldAt(9.5f);',                                   # 字面量（半点）
        'MonoAt(10.5f);',                                      # 字面量（第三族）
        'FontAt(-1.0f);',                                      # 字面量（负号，保守多报）
        'FontAt(.5f);'                                         # 字面量（前导点）
    )
    $fakes = @(
        'draw->AddText(FontAt(theme::font::kBody), kBody, p, c, t);',  # AddText 在，但字号是 token ⇒ 只中 AddText 一条
        'FontAt(theme::font::kLabel);',                        # token 实参
        'FontAt(textSize);',                                   # 变量实参
        'DrawTextClipped(draw, FontAt(12.0f), 12.0f, a, b);',  # 共享原语 + 字面量 ⇒ 仍要中 Font 一条
        '// draw->AddText(...) 整行注释不许报',
        '/* FontAt(11.5f) 块注释不许报 */',
        '// 旧代码写的是 FontAt(11.5f)，行尾注释后的代码部分才算',
        'const char* k = "AddText(FontAt(11.5f))";'            # 字符串字面量不许报
    )
    [IO.File]::WriteAllBytes((Join-Path $dir 'samples.cpp'), $enc.GetBytes((($reals + $fakes) -join "`n")))

    $got = @(& $PSCommandPath -Root $dir 2>&1 |
             Select-String -Pattern 'raw-text hits: (\d+)')
    $n = if ($got.Count -gt 0) { [int]$got[0].Matches[0].Groups[1].Value } else { -1 }
    Remove-Item $dir -Recurse -Force
    # 期望数按判据现推：真样本 8 行 = 9 条（第 3 行 AddText 与 FontBoldAt 各中一条）；
    # 假样本里两条**有意**命中 —— 「AddText(token)」中 AddText 一条（裸调用本身违禁，
    # 字号是 token 只救字号那条）、「DrawTextClipped(FontAt(12.0f),…)」中 Font 字面量
    # 一条（共享原语不豁免它**实参**里的字面量）⇒ 9 + 2 = 11。
    # 两个数字都按判据推出来写在这，改判据时先改这里、再核样本。
    $expect = 11
    if ($n -ne $expect) {
        Write-Output "find-raw-text 自检 FAILED：期望命中 $expect，实得 $n"
        exit 1
    }
    Write-Output "find-raw-text 自检 PASS（8 真 + 8 假样本，命中数 $n = 期望 $expect）"
    exit 0
}

# =====================================================================
# 正常路径
# =====================================================================
$ErrorActionPreference = 'Continue'
$pagesRoot = if ($Root) { $Root } else { Join-Path $PSScriptRoot '..\src\ui\imgui\pages' }
if (-not (Test-Path $pagesRoot)) {
    Write-Output "find-raw-text: 找不到 $pagesRoot"
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
        # 同名子目录会撞名 —— 现状 pages 是平的；真撞了再换相对路径，别提前复杂化。
        $perFile[$rel] = $hits.Count
        foreach ($h in $hits) { $all += [pscustomobject]@{ File = $rel; Line = $h.Line; Kind = $h.Kind; Text = $h.Text } }
    }
}

$baselinePath = Join-Path $PSScriptRoot 'find-raw-text.baseline.txt'
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
    $lines = @('# find-raw-text 棘轮基线：文件=存量条数。收编一个文件就跑 -WriteBaseline 收紧，只降不升。')
    foreach ($k in ($perFile.Keys | Sort-Object)) { $lines += "$k=$($perFile[$k])" }
    # ⚠️ 带 BOM 写：本机 PowerShell 5.1 的 Get-Content 不带 -Encoding 就按 ANSI 读，
    #    无 BOM 的中文注释在别人眼里是乱码（AGENTS.md 记过同款坑）。
    [IO.File]::WriteAllLines($baselinePath, $lines, (New-Object System.Text.UTF8Encoding($true)))
    Write-Output "find-raw-text: 基线已写入 $baselinePath（$($perFile.Count) 个文件，共 $(($perFile.Values | Measure-Object -Sum).Sum) 条）"
    exit 0
}

foreach ($h in $all) {
    "{0}:{1}  [{2}]" -f $h.File, $h.Line, $h.Kind
    "    $($h.Text)"
}
''
"--- raw-text hits: $($all.Count) ---"

# 逐文件对基线：超了才算新增违规。
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
    Write-Output '页面层禁止裸 AddText（一律 kit::Text）与 FontAt/FontBoldAt/MonoAt(字面量)（一律 theme::font::* token）。'
    exit 1
}
if ($all.Count -gt 0) {
    Write-Output "find-raw-text: PASS（$($all.Count) 条存量全部在基线内；收编后跑 -WriteBaseline 收紧棘轮）"
} else {
    Write-Output 'find-raw-text: PASS（0 处裸文字 / 字面量字号）'
}
exit 0
