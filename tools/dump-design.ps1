# 设计稿几何取数（R1，refactor/记忆.md（原 记忆.md（原 component-reuse.md）） §6）
#
# 从 **真的 React app**（webui/dist）里量出每个控件的几何，产出 out\design-geometry.json。
# 取回来的数分两栏：`raw`（浏览器算出来的原值）与 `snap`（按取整规则 snap 后的值）。
#
# 为什么要有这个脚本：
#   * 之前每个几何常量都是「读 CSS → 手推算式 → 抄进页面」，同一个 CSS 组件在不同页面
#     被重推一遍，而 `11.5 × 1.6`、`auto-fit minmax(210px,1fr)` 这类算式会产出
#     `115.171875` 这种数 —— 抠它不是精度需要，是「真值散在 CSS 里、靠人搬运」。
#   * 探针**不手抄 DOM**：点的是 app 自己的控件（`.proj-card` = openProject、
#     `.rail-btn` = 切工作区），量的是 app 自己渲染出来的节点。手抄的静态 HTML
#     一旦和 JSX 有出入，量到的就不是设计稿。
#
# 取数走 **agent-browser**（IDE 自带的浏览器 CLI），不用 Chrome 无头。两条原因都是实测踩出来的：
#   1. **精确视口**：`chrome --headless=new --window-size=1600,960` 实测给到的是
#      `innerWidth/innerHeight = 1582×808`（窗口尺寸 ≠ 视口），而 C++ 基线是 1600×960。
#      agent-browser 的 `set viewport 1600 960` 是**内容区**尺寸，量到的就是设计稿在
#      那个宽度下的排布（`auto-fit` 的列数、换行位置都由它决定）。
#   2. **eval 的返回值就是数据**：老路是「探针把结果写进 `<body data-probe="...">` →
#      `--dump-dom` 取 → 正则抠出来 → HTML 反转义 → `ConvertFrom-Json`」。那一段里
#      有三处会静默改数据：`&` 被 HTML 转义成 `&amp;`、PS 5.1 按系统码页（GBK）解码
#      无 BOM 的 UTF-8 会把「多字节字符后面的那个 ASCII 字节」一并吃掉、以及
#      `&` 一旦丢了 JSON 字符串就被撕开（报的却是 `':' or '}' expected`，看着像探针坏了）。
#      现在结果直接当返回值出来，一次 `ConvertFrom-Json` 就完事。
#
# 取整规则（与 记忆.md（原 component-reuse.md） §6.3 一致，改这里要同步那份文档）：
#   * 布局尺寸（w / h）与间距档（padding / gap）→ snap 到 **2px 网格**
#   * 字号 / 字距 → snap 到 **0.5px**（整数字号会让半点设计档被向上取档画大，本仓踩过）
#   * **不 snap**：border 宽度（1px 就是 1px）、圆角档（4/6/10/999）、位置 x/y、基线、墨迹
#   * 差值 ≤ 1px 一律不追，记进 design-spec.md §9.3 —— 这是规则，不是偷懒
#
# 用法:
#   powershell -File tools\dump-design.ps1                       # 默认：总控 / 资产 / 出图
#   powershell -File tools\dump-design.ps1 -Views overview       # 只跑一个
#   powershell -File tools\dump-design.ps1 -SelfTest             # 自检（判据不许只报绿）
param(
    [string[]]$Views = @('overview', 'assets', 'image'),
    [string]$OutFile = (Join-Path $PSScriptRoot '..\out\design-geometry.json'),
    [int]$Port = 5199,
    [int[]]$Viewport = @(1600, 960),   # 内容区尺寸，与 C++ 取证基线一致（不是窗口尺寸）
    [string]$Session = 'shine-design', # agent-browser 的会话名（隔离，别串到别的页面上）
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$script:TraceLog = Join-Path (Split-Path -Parent $PSScriptRoot) 'out\_design_probe\_trace.log'
function Trace([string]$m) { Add-Content -Path $script:TraceLog -Value "$((Get-Date).ToString('HH:mm:ss')) $m" -Encoding UTF8 }
Trace "start; PSScriptRoot=$PSScriptRoot; PSVersion=$($PSVersionTable.PSVersion)"
$repo = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repo 'webui\dist'
$probeDir = Join-Path $repo 'out\_design_probe'

function Fail([string]$msg) { Trace "FAIL: $msg"; Write-Host "dump-design: FAIL - $msg" -ForegroundColor Red; exit 1 }
# ⚠️ break 会把整轮杀掉；改成 continue = 记下来继续跑 —— 各 view 自带 ok/error 标记、
#    结尾还有对账自检，单点失败不需要陪葬（实测 trap+break 杀死过整轮）。
trap { Trace "TRAP line $($_.InvocationInfo.ScriptLineNumber): $($_.Exception.Message)"; continue }

Trace "paths repo=$repo dist=$dist existsIndex=$(Test-Path (Join-Path $dist 'index.html'))"

# ---------------------------------------------------------------- 前置检查
if (-not (Test-Path (Join-Path $dist 'index.html'))) {
    Fail "没有 $dist\index.html。webui/ 不在版本控制里，新机器要先 `cd webui && npm run build`。"
}
foreach ($f in @('serve.js', 'probe.js', 'probe-entry.js')) {
    if (-not (Test-Path (Join-Path $probeDir $f))) { Fail "缺 $probeDir\$f" }
}
$node = (Get-Command node -ErrorAction SilentlyContinue).Source
if (-not $node) { Fail "找不到 node（起静态服务要用）" }
$ab = (Get-Command agent-browser -ErrorAction SilentlyContinue).Source
if (-not $ab) {
    Fail "找不到 agent-browser（取数用它驱动浏览器）。装法：npm install -g agent-browser && agent-browser install"
}

# dist 的产物名带 hash，每次 build 都会变 ⇒ 现查，不写死。
$jsFiles = @(Get-ChildItem -Path (Join-Path $dist 'assets') -Filter 'index-*.js')
$cssFiles = @(Get-ChildItem -Path (Join-Path $dist 'assets') -Filter 'index-*.css')
Trace "assets exists=$(Test-Path (Join-Path $dist 'assets')) jsN=$($jsFiles.Count) cssN=$($cssFiles.Count)"
if ($jsFiles.Count -ne 1 -or $cssFiles.Count -ne 1) {
    Fail "dist\assets 里有 $($jsFiles.Count) 个 js / $($cssFiles.Count) 个 css（应为各 1 个）。多半是旧的产物没清：进 webui 重新 npm run build 一次。"
}
$jsName = $jsFiles[0].Name
$cssName = $cssFiles[0].Name

# ---------------------------------------------------------------- 每个 view 的配置（选择器是真 class 名，从 JSX 里取的）
$viewCfg = @{
    overview = @{
        railIndex = 0
        ready     = '.kpis'
        selectors = @('.kpis', '.card.kpi', '.kpi .k-label', '.kpi .k-value', '.kpi .k-foot',
            '.grid-3-1', '.card-h', '.card-title', '.gantt', '.ghead', '.table', '.table th',
            '.table td', '.list-row')
        pseudo    = @(@{ sel = '.kpi'; pseudo = '::after' })
    }
    assets   = @{
        railIndex = 2
        ready     = '.asset-grid'
        selectors = @('.asset-grid', '.asset-grid > *', '.aname', '.ameta', '.cap', '.chip',
            '.drow', '.dnode', '.dthumb', '.compare', '.derive')
        pseudo    = @()
    }
    image    = @{
        railIndex = 4
        ready     = '.canvas-page'
        selectors = @('.canvas-page', '.fp-h', '.fp-b', '.fp-f', '.float-toolbar', '.drow',
            '.dlabel', '.checklist', '.ck-ic')
        pseudo    = @()
    }
}
# 打开检查器（rail 上第 9 个按钮：6 工作区 + 分隔线 + 侧栏/底栏/检查器三个开关，
# 见 webui/src/shell/Shell.jsx:99-108）⇒ 内容宽度与 C++ 基线（1600×960 + 检查器打开）一致。
$preSteps = @(@{ sel = '.rail-btn'; i = 8 })
$probeJs = Join-Path $probeDir 'probe.js'
$entryTpl = Get-Content (Join-Path $probeDir 'probe-entry.js') -Raw -Encoding UTF8

# ---------------------------------------------------------------- 起服务（已有就复用）
# ⚠️ EADDRINUSE 在 trace 里栽过三次（01:54 / 12:09 / 12:13）：上一轮 Cleanup 漏杀、
#    或上一条管道（_run_all_pages2.ps1，README 写明「已在跑就复用」）先起过同一个端口。
#    所以这里先探测：端口活着就当它是我们的 serve.js 复用（不杀 —— 可能是别的管道起的），
#    死了才自己起。复用时要验证它真的在服务 dist（探针页面打不开就等于量了个寂寞）。
$server = $null
$portLive = $false
try {
    $tcp = New-Object Net.Sockets.TcpClient
    $tcp.Connect('127.0.0.1', $Port)
    $portLive = $tcp.Connected
    $tcp.Close()
} catch { $portLive = $false }
if ($portLive) {
    Trace "port $Port already serving - reusing"
    Write-Host "  端口 $Port 已有服务在跑，复用（不另起）"
}
else {
    $server = Start-Process -FilePath $node -ArgumentList @(
        (Join-Path $probeDir 'serve.js'), $dist, "$Port"
    ) -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $probeDir '_serve.out') -RedirectStandardError (Join-Path $probeDir '_serve.err')
    $ok = $false
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 150
        if ((Test-Path (Join-Path $probeDir '_serve.out')) -and
            ((Get-Content (Join-Path $probeDir '_serve.out') -Raw -ErrorAction SilentlyContinue) -match 'ready')) { $ok = $true; break }
    }
    if (-not $ok) {
        if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
        # 把 stderr 的首行带进消息：EADDRINUSE（端口被上一轮遗留的 node 占着，实测踩过）
        # 与「node 起不来」是两件事，只报「没起来」会把它们混成一个。
        $why = ''
        $errPath = Join-Path $probeDir '_serve.err'
        if (Test-Path $errPath) {
            $first = (Get-Content $errPath -ErrorAction SilentlyContinue | Where-Object { $_ -match '\S' } | Select-Object -First 1)
            if ($first) { $why = " 原因: $first" }
        }
        Fail "静态服务没起来（端口 $Port）。$why  详见 $errPath（占端口就用 -Port 换一个）"
    }
}

# agent-browser 的调用壳：每个 view 一个会话，命令之间靠守护进程保持浏览器状态。
function AB([string]$sess, [string[]]$cmdArgs) {
    $all = @('--session', $sess) + $cmdArgs
    # ⚠️ $ErrorActionPreference='Stop' 时，native 命令写到 stderr 的**任何内容**经 2>&1
    #    都会变成终止错误（NativeCommandError）—— CLI 的进度/警告全在 stderr ⇒
    #    一次非致命输出就把整个脚本炸掉，后面的重试循环根本没机会跑（实测 10060 那条）。
    #    所以这里把 EAP 压回 Continue，错误与否由返回值内容判。
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { return (& $ab @all 2>&1 | Out-String) }
    finally { $ErrorActionPreference = $prev }
}

function Cleanup {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    # 浏览器也要关：守护进程会一直留着（本仓的纪律是「收尾不留僵尸」）。
    foreach ($v in $Views) { [void](AB -sess "$Session-$v" -cmdArgs @('close')) }
    [void](AB -sess $Session -cmdArgs @('close'))
    [void](AB -sess "$Session-warm" -cmdArgs @('close'))
    Remove-Item -Force (Join-Path $probeDir '_serve.out') -ErrorAction SilentlyContinue
    Remove-Item -Force (Join-Path $probeDir '_serve.err') -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------- 暖机（吸收首次拉起 Chromium 的超时）
# ⚠️ 实测（trace 12:10/12:12 两条 10060）：守护进程没带浏览器时，第一条 `open` 要先拉起
#    Chromium，耗时超过 CLI 的读超时 ⇒ 命令报「Failed to read (os error 10060)」。
#    而浏览器一旦活着，后续 open 都是毫秒级。所以在正式取数前先用一次性会话把浏览器
#    拉起来（带重试：第一次超时后浏览器进程已在路上，第二次必成）。
$warm = $false
for ($i = 0; $i -lt 4 -and -not $warm; $i++) {
    if ($i -gt 0) { Start-Sleep -Seconds 3 }
    if ($i -eq 0) {
        # 第一阶段：只起守护进程（eval 不碰浏览器，秒回）—— 冷启动 = 起守护进程 + 拉
        # Chromium 两段叠加，会超过 CLI 的读超时；拆开让第二阶段只剩拉浏览器一段。
        [void](AB -sess "$Session-warm" -cmdArgs @('eval', '1+1'))
    }
    $out = AB -sess "$Session-warm" -cmdArgs @('open', 'about:blank')
    if ($out -notmatch '✗|ERROR') { $warm = $true }
}
if (-not $warm) { Fail "浏览器暖机失败（3 次尝试都没能 open about:blank）。daemon 可能挂了：先 taskkill 掉 agent-browser 再跑。" }
# ⚠️ 这里**不要 close**：close 会把浏览器整个关掉，紧接着 view 循环的第一次 open
#    又变回冷启动、再次撞 10060（实测 12:21:53 就是这么死的）。浏览器留给后面复用，
#    Cleanup 里统一收。
Trace "browser warmed up"

# ---------------------------------------------------------------- 跑每个 view
$results = @{}
$url = "http://127.0.0.1:$Port/index.html"
try {
    foreach ($v in $Views) {
        if (-not $viewCfg.ContainsKey($v)) { Write-Host "  跳过未知 view: $v"; continue }
        $cfg = $viewCfg[$v]
        $cfgJson = @{
            view      = $v
            railIndex = $cfg.railIndex
            ready     = $cfg.ready
            selectors = $cfg.selectors
            pseudo    = $cfg.pseudo
            preSteps  = $preSteps
        } | ConvertTo-Json -Depth 5 -Compress

        $entry = $entryTpl.Replace('__CONFIG__', $cfgJson)
        # ⚠️ 走 `eval -b`（base64）把脚本递进去，两条都是有原因的：
        #    * `eval --stdin` **在本机是坏的**：实测 `'1+1' | agent-browser eval --stdin` 恒返回
        #      `null`（内联 `eval "1+1"` 才是 2）。管道还额外过一道 PowerShell 的
        #      `$OutputEncoding`（5.1 默认是 ASCII，非 ASCII 全变 `?`）。
        #    * 直接内联当命令行参数也不行：脚本是多行、且含引号，过 shell 转义必然出错。
        #    base64 之后是纯 ASCII 的单个 token，绕过上面两条，字节一个不改。
        $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($entry))

        # ⚠️ **每个 view 一个干净会话**，且它的第一条命令必须是 `--init-script ... open <url>`：
        #    init script 只在那一次调用触发的导航上被注入（见 probe-entry.js 顶部的实测记录）。
        #    顺带解决「摆好页面态」：新会话 = 新页面，不会带着上一个 view 的点击。
        #    视口在导航**之后**设：`set viewport` 会即时重排（实测在已有页面上设 1600×960，
        #    紧接着量到的就是 1600×960）；放在导航之前则要依赖会话的初始状态。
        $sess = "$Session-$v"
        # 每步都打时间戳：浏览器这层慢/挂的时候，「卡在哪一步」只能靠这个区分
        # （实测有过一次整轮挂住、trace 只到 "assets exists" 就没了的情况，
        #  事后完全分不清是起服务、开页面还是 eval）。
        $sw = [Diagnostics.Stopwatch]::StartNew()
        # ⚠️ open 也要带重试：浏览器被 close 过（上一轮残留 / 会话收尾）时第一次 open
        #    仍是冷启动，会撞 CLI 读超时（10060）。有暖机兜着通常一次过。
        $openOut = ''
        for ($r = 0; $r -lt 3; $r++) {
            if ($r -gt 0) { Start-Sleep -Seconds 3 }
            $openOut = AB -sess $sess -cmdArgs @('--init-script', $probeJs, 'open', $url)
            if ($openOut -notmatch '✗|ERROR') { break }
        }
        Trace "$v open tries=$($r+1) $($sw.ElapsedMilliseconds)ms"; $sw.Restart()
        [void](AB -sess $sess -cmdArgs @('set', 'viewport', "$($Viewport[0])", "$($Viewport[1])"))
        Trace "$v viewport $($sw.ElapsedMilliseconds)ms"; $sw.Restart()

        $raw = (AB -sess $sess -cmdArgs @('eval', '-b', $b64))
        Trace "$v eval $($sw.ElapsedMilliseconds)ms"
        [void](AB -sess $sess -cmdArgs @('close'))
        # agent-browser 把返回值按 JSON 编码打出来 ⇒ 我们的 JSON.stringify 结果外面还套了一层
        # 字符串字面量，所以要解两次：第一次拿到 JSON 文本，第二次拿到对象。
        $jsonText = $null
        try { $jsonText = $raw.Trim() | ConvertFrom-Json } catch { }
        if ($null -eq $jsonText -or $jsonText -isnot [string]) {
            Trace "eval 返回无法解析 len=$($raw.Length) head=$($raw.Substring(0, [Math]::Min(200, $raw.Length)))"
            Write-Host "  $v : 取数失败（eval 没返回可解析的 JSON）" -ForegroundColor Red
            $results[$v] = @{ ok = $false; error = 'bad eval output' }
            continue
        }
        Trace "eval len=$($jsonText.Length)"
        $parsed = $jsonText | ConvertFrom-Json
        $results[$v] = $parsed
        Write-Host "  $v : $(if($parsed.ok){'ok'}else{'ERROR ' + $parsed.error})  元素 $($parsed.elements.Count) 个  伪元素 $($parsed.pseudo.Count) 个  视口 $($parsed.viewport -join 'x')"
    }
}
finally {
    Cleanup
}
# ⚠️ 这里的括号数是踩过的坑：少一个会报「缺少右 }」并把行号指向 `try` 那一行，
#    照着行号去改就会改错地方。数括号请用 token（[Parser]::ParseFile 的第 2 个 out 参数）。
# ⚠️ 另一个同类坑：本文件**必须带 UTF-8 BOM**。PowerShell 5.1 读无 BOM 的 UTF-8 会按
#    系统 ANSI 码页（GBK）解码，中文被拆出坏字节，后面的行会被静默吞掉 ——
#    症状是「执行跳行」，而不是报错。本仓带中文的 ps1 一律带 BOM。

# ---------------------------------------------------------------- snap（取整规则）
function Snap2([double]$v) { if ($null -eq $v) { return $null } return [Math]::Round($v / 2.0) * 2.0 }
function SnapHalf([double]$v) { if ($null -eq $v) { return $null } return [Math]::Round($v / 0.5) * 0.5 }

$out = [ordered]@{
    generated      = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')
    source         = [ordered]@{ dist = 'webui/dist'; js = $jsName; css = $cssName; driver = 'agent-browser'; session = $Session }
    viewport       = $Viewport
    baselineNote   = '元素的 baseline 是**相对本元素顶**的偏移；设计稿那三条（28/66.39/94.78）是相对 KPI **卡**顶的，比较时要加 (元素 y − 卡 y)'
    snapRule       = [ordered]@{
        size      = 'w/h/padding/gap -> 2px 网格'
        font      = 'size/letterSpacing -> 0.5px'
        untouched = 'borderWidth/radius(4|6|10|999)/x/y/baseline/ink 不 snap'
        tolerance = '差值 <= 1px 不追，记入 design-spec.md §9.3'
    }
    views          = @{}
}
foreach ($v in $results.Keys) {
    $r = $results[$v]
    if (-not $r.ok) { $out.views[$v] = @{ ok = $false; error = $r.error }; continue }
    $els = @()
    foreach ($e in $r.elements) {
        $rec = [ordered]@{
            sel      = $e.sel
            index    = $e.index
            text     = $e.text
            rect     = [ordered]@{ x = $e.rect.x; y = $e.rect.y; w = $e.rect.w; h = $e.rect.h; wSnap = (Snap2 $e.rect.w); hSnap = (Snap2 $e.rect.h) }
            font     = [ordered]@{
                size          = $e.font.size
                sizeSnap      = (SnapHalf $e.font.size)
                weight        = [int]$e.font.weight
                family        = $e.font.family
                lineHeight    = $e.font.lineHeight
                letterSpacing = $e.font.letterSpacing
                numeric       = $e.font.numeric
            }
            box      = [ordered]@{
                padT = $e.box.padT; padR = $e.box.padR; padB = $e.box.padB; padL = $e.box.padL
                padTSnap = (Snap2 $e.box.padT); padLSnap = (Snap2 $e.box.padL)
                gap = $e.box.gap; gapSnap = (Snap2 $e.box.gap)
                radius = $e.box.radius; borderW = $e.box.borderW
            }
            color    = $e.color
            bg       = $e.bg
            border   = $e.border
            overflow = $e.overflow
        }
        if ($e.PSObject.Properties.Name -contains 'baseline') {
            $rec.baseline = $e.baseline
            $rec.ink = $e.ink
        }
        $els += $rec
    }
    $out.views[$v] = [ordered]@{
        ok       = $true
        theme    = $r.theme
        viewport = $r.viewport
        dpr      = $r.dpr
        notes    = $r.notes
        elements = $els
        pseudo   = $r.pseudo
    }
}

# ⚠️ 默认参数是「相对 $PSScriptRoot」拼出来的**绝对路径**，再 Join-Path 一次会得到
#    `E:\repo\E:\repo\out\...` ⇒ "The given path's format is not supported"。
$outPath = if ([System.IO.Path]::IsPathRooted($OutFile)) { $OutFile }
else { [System.IO.Path]::GetFullPath((Join-Path $repo $OutFile)) }
[System.IO.File]::WriteAllText($outPath, ($out | ConvertTo-Json -Depth 12), (New-Object System.Text.UTF8Encoding($false)))
Write-Host "已写出 $outPath"

# ---------------------------------------------------------------- 自检：与手工量过的数对得上
# 第十一轮手工量过（Chrome 探针 + 逐像素复核）的三个数，是这条管道唯一的外部锚点。
# ⚠️ 基线口径：探针给的是**相对元素顶**，第十一轮给的是**相对 KPI 卡顶** ⇒
#    这里必须加 (元素 y − 卡 y)。第一版直接拿元素相对值比，读到 13 ≠ 28 就报红，
#    看起来像探针量错了（实际是口径没换算）。
# ⚠️ 判据必须能红：下面除了「真值要过」，还要验证「扰动 +1px 必须红」。
function CheckKpi($expectedH, $expectedBases) {
    $v = $out.views['overview']
    if (-not $v -or -not $v.ok) { return @{ pass = $false; why = 'overview 没取到数' } }
    $kpi = $v.elements | Where-Object { $_.sel -eq '.card.kpi' -and $_.index -eq 0 } | Select-Object -First 1
    if (-not $kpi) { return @{ pass = $false; why = '没量到 .card.kpi' } }
    if ([Math]::Abs($kpi.rect.h - $expectedH) -gt 0.05) {
        return @{ pass = $false; why = "卡高 $($kpi.rect.h) ≠ 期望 $expectedH" }
    }
    foreach ($pair in $expectedBases) {
        $el = $v.elements | Where-Object { $_.sel -eq $pair.sel -and $_.index -eq 0 } | Select-Object -First 1
        if (-not $el) { return @{ pass = $false; why = "没量到 $($pair.sel)" } }
        $inCard = ($el.rect.y - $kpi.rect.y) + $el.baseline
        $inCard = [Math]::Round($inCard, 4)
        Write-Host ("      {0,-16} 卡内基线 {1}" -f $pair.sel, $inCard)
        if ([Math]::Abs($inCard - $pair.y) -gt 0.05) {
            return @{ pass = $false; why = "$($pair.sel) 卡内基线 $inCard ≠ 期望 $($pair.y)" }
        }
    }
    return @{ pass = $true; why = '卡高与三条基线都对上' }
}

$truth = @(
    @{ sel = '.kpi .k-label'; y = 28.0 },
    @{ sel = '.kpi .k-value'; y = 66.390625 },
    @{ sel = '.kpi .k-foot'; y = 94.78125 }
)
$good = CheckKpi 115.171875 $truth
if ($good.pass) {
    Write-Host "自检 PASS：KPI 卡高 115.171875 与三条卡内基线（28 / 66.390625 / 94.78125）与第十一轮手工量的一致" -ForegroundColor Green
}
else {
    Write-Host "自检 FAIL：$($good.why)" -ForegroundColor Red
}

if ($SelfTest) {
    # 判据不许只报绿：把期望的卡高挪 1px，它必须红。
    $bad = CheckKpi 116.171875 $truth
    if ($bad.pass) { Write-Host "自检 FAIL：卡高扰动 +1px 仍然判绿 —— 这条判据是恒真的" -ForegroundColor Red; exit 1 }
    Write-Host "自检 PASS：卡高扰动 +1px 能判红（判据不是恒真）" -ForegroundColor Green
}

if (-not $good.pass) { exit 1 }
