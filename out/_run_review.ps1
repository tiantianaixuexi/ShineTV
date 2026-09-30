# 临时驱动：跑一轮 SHINE_IMGUI_REVIEW 并回显退出码 + manifest。
#
# 用法:
#   powershell -NoProfile -ExecutionPolicy Bypass -File out\_run_review.ps1 -OutDir <dir> [-Exe <exe>] [-Theme ink]
#
# ⚠️ 本文件**必须带 UTF-8 BOM**。PowerShell 5.1 读无 BOM 的 .ps1 会按系统 ANSI 码页
#    （中文 Windows = GBK/936）解码，里面每一行中文注释都会被拆成错字节；某些字节组合
#    会凑出引号 / 括号，把后面的代码吃掉，表现是 `ParserError: Unexpected token ')'`
#    —— 而报错行号指向的地方跟真正的原因毫无关系。写 .ps1 一律带 BOM。
param(
    # ⚠️ 早先这四个默认值全是**硬编码的绝对路径**（`E:\c++\ShineTV\...`）。脚本进了版本
    #    控制之后这就成了换机器即失效的默认值 —— 别人 clone 下来直接跑会静默去读一个
    #    不存在的盘。改成从仓库根 + $PSScriptRoot 推出来，本机不用传参也能跑。
    [string]$OutDir = (Join-Path $PSScriptRoot 'review'),
    [string]$Exe    = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build-p7\ShineTVStudio.exe'),
    [string]$Theme  = "ink",
    # 取证用的 novel.db 源（由 _make_fixture.ps1 生成）。
    [string]$FixtureDb = (Join-Path $PSScriptRoot '_fixture\db\novel.db')
)

$ErrorActionPreference = 'Stop'

# ---- 1. 先把 fixture 库拷进取证输出目录 ----
#
# ⚠️ 这一步以前是**手工**做的：SeedReportFixture() 只写 work/ch<NNN>/v08_continuity.json，
#    不建 novel.db。少了这个库，侧栏树 / 检查器 / 资产树会全部退化成诚实空态 ——
#    而旧门禁只查「图写出来了」，所以整轮照样 PASS，只是图是空的。
#    现在并进脚本：缺库直接失败，不给「静默拍到空态」留口子。
if (-not (Test-Path $FixtureDb)) {
    throw "fixture 库不存在：$FixtureDb。先跑 out\_make_fixture.ps1 生成。"
}
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir "_review_reports_project\db") | Out-Null
Copy-Item -Force $FixtureDb (Join-Path $OutDir "_review_reports_project\db\novel.db")
# 自检文件跟着库一起搬 —— 证据和数据必须同进同出，否则事后只看得到库、看不到
# 当时那几行的数字。
$checkSrc = Join-Path (Split-Path -Parent $FixtureDb) "fixture-selfcheck.txt"
if (-not (Test-Path $checkSrc)) {
    throw "fixture 自检文件不存在：$checkSrc。重新跑 out\_make_fixture.ps1。"
}
Copy-Item -Force $checkSrc (Join-Path $OutDir "_review_reports_project\db\fixture-selfcheck.txt")
# 显式 UTF8：Get-Content 在 PS 5.1 下默认按 ANSI 读，自检文件是无 BOM UTF-8，
# 不给编码会把「第七封信的邮戳」显示成乱码 —— 看着像 fixture 建错了，其实只是读错了。
Write-Host "fixture: $((Get-Content -Encoding UTF8 $checkSrc) -join ' | ')"

# ---- 2. 跑取证 ----
$env:SHINE_IMGUI_REVIEW = $OutDir
$env:SHINE_THEME_SET = $Theme
# ⚠️ 这里必须把 ErrorActionPreference 降回 Continue：exe 只要往 stderr 写一行日志，
#    'Stop' 就会把它当终止错误，脚本在取证**还没跑完**时直接退出 —— 表现是「没 manifest」，
#    而真正的原因（日志）被当成异常吞掉了。上面那些步骤保持 'Stop'，它们失败就该立刻停。
$ErrorActionPreference = 'Continue'
& $Exe *>&1 | ForEach-Object { "$_" } | Set-Content -Path "$OutDir.stdout.txt" -Encoding UTF8
$code = $LASTEXITCODE
Write-Host "EXITCODE=$code"

# ---- 3. 回显 manifest ----
$manifest = Join-Path $OutDir "shots-manifest.txt"
if (Test-Path $manifest) {
    Get-Content $manifest | ForEach-Object { Write-Host $_ }
} else {
    Write-Host "NO MANIFEST"
}
exit $code
