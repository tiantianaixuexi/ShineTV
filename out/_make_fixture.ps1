# 临时驱动：从 schema + data + seed 重建一份 novel.db fixture 供取证用。
#
# 三个 SQL 都是 UTF-8，**必须**由 sqlite3 自己读（.read），不要在 PowerShell 里
# Get-Content 再喂进去 —— 那一步就会按 ANSI 码页重编码，中文全变乱码。
#
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File out\_make_fixture.ps1 -Db <path>
param(
    # ⚠️ 早先是硬编码的 `E:\c++\ShineTV\out\...`。脚本进了版本控制之后这就成了换机器
    #    即失效的默认值 —— 改成从 $PSScriptRoot 推出来。
    [string]$Db = (Join-Path $PSScriptRoot '_fixture\db\novel.db')
)

$ErrorActionPreference = 'Stop'
$sqlite = "C:\msys64\mingw64\bin\sqlite3.exe"
if (-not (Test-Path $sqlite)) { throw "sqlite3 不在 $sqlite" }

$dir = Split-Path -Parent $Db
New-Item -ItemType Directory -Force -Path $dir | Out-Null
if (Test-Path $Db) { Remove-Item -Force $Db }

foreach ($f in @("_fixture_schema.sql", "_fixture_data.sql", "_fixture_seed.sql")) {
    $path = Join-Path $PSScriptRoot $f
    if (-not (Test-Path $path)) { throw "缺 $path" }
    & $sqlite $Db ".read $path"
    if ($LASTEXITCODE -ne 0) { throw "$f 应用失败" }
}

# 自检：伏笔行真的落进去了，且中文没被编码毁掉。
# 逐条查、**先赋值再输出** —— 把 & $sqlite 直接嵌进字符串里时，本机 PowerShell 5.1
# 会零星吞掉整行（实测 8 行少 2 行，且不报错）。取证自检丢输出比不写更糟，所以改成
# 显式赋值。
$e  = & $sqlite $Db 'select count(*) from entities;'
$sc = & $sqlite $Db 'select count(*) from scenes;'
$sh = & $sqlite $Db 'select count(*) from shots;'
$of = & $sqlite $Db "select count(*) from foreshadowings where status in ('PLANNED','PLANTED','DEVELOPING');"
$sf = & $sqlite $Db 'select count(*) from scene_foreshadows;'
$ek = & $sqlite $Db 'select kind || ''='' || count(*) from entities group by kind;'
# ---- 覆盖度自检（2026-09-30 加）----
# 这几条盯的不是「行数够不够」，而是**枚举值铺没铺开** —— 页面拍到了不等于分支跑到了，
# 而「某个状态一次都没出现过」的 fixture 会让对应的分支永远没人验。
#   $va  visual_assets 行数          $vs 其中**不同 status 的个数**（状态机 8 值 ⇒ 期望 8）
#   $aa visual_artifacts 行数（扩充前是 0！）  $as 层状态个数（4 值 ⇒ 期望 4）
#   $al 层名个数（kChain 4 层 ⇒ 期望 4）      $ad 带降级标记的产物数（期望 > 0）
$va = & $sqlite $Db 'select count(*) from visual_assets;'
$vs = & $sqlite $Db 'select count(distinct status) from visual_assets;'
$aa = & $sqlite $Db 'select count(*) from visual_artifacts;'
$as = & $sqlite $Db 'select count(distinct status) from visual_artifacts;'
$al = & $sqlite $Db 'select count(distinct layer) from visual_artifacts;'
$ad = & $sqlite $Db 'select count(*) from visual_artifacts where degraded=1;'
$vc = & $sqlite $Db 'select count(*) from chapters;'
$vv = & $sqlite $Db 'select count(*) from volumes;'
# 下面两行是**编码自检**：中文正常就说明没被 GBK 码页重编码过。
$f1 = & $sqlite $Db 'select title from foreshadowings where id=1;'
$t1 = & $sqlite $Db 'select title from scenes where id=1;'

"entities          = $e"
"scenes            = $sc"
"shots             = $sh"
"chapters/volumes  = $vc / $vv"
"open_foreshadows  = $of"
"scene_foreshadows = $sf"
"entity kinds      = $($ek -join ', ')"
"visual_assets     = $va  (distinct status = $vs / 8)"
"visual_artifacts  = $aa  (status = $as / 4, layer = $al / 4, degraded = $ad)"
"foreshadow#1      = $f1"
"scene#1 title     = $t1"

# 硬门禁：数量不对就直接失败，别让一个残缺的 fixture 跑完 80 多张取证图。
if ("$e/$sc/$sh/$of/$sf" -ne "17/8/19/2/3") { throw "fixture 行数不对：$e/$sc/$sh/$of/$sf（期望 17/8/19/2/3）" }
# 覆盖度门禁：**枚举值必须铺满**。这三条挡的是「页面拍到了、分支没跑到」——
# 比如 `visual_artifacts` 一行都没有时（2026-09-30 之前就是这样）派生链、层状态、
# 降级三块在证据图里一次都跑不到，而整轮照样 PASS。
if ($vs -ne 8) { throw "visual_assets 只覆盖了 $vs/8 种状态（状态机 8 值必须都出现）" }
if ($as -ne 4) { throw "visual_artifacts 只覆盖了 $as/4 种层状态（PENDING|RUNNING|DONE|FAILED）" }
if ($al -ne 4) { throw "visual_artifacts 只覆盖了 $al/4 层（kChain: front|turnaround|base_body|wardrobe）" }
if ($ad -lt 1) { throw "没有一条 degraded=1 的产物 ⇒ K28「降级必须可见」那条分支验不到" }

# 自检同时落一份盘。控制台回显在本机会零星吞掉整行（`entities` / `foreshadow#1`
# 两行稳定显示为空，但门禁比字符串证明它们的值是对的）—— 门禁过了不等于**看得见**，
# 所以把同一份数字写进文件，取证报告直接引这个文件，不引控制台。
$check = @(
    "entities=$e", "scenes=$sc", "shots=$sh",
    "chapters=$vc", "volumes=$vv",
    "open_foreshadows=$of", "scene_foreshadows=$sf",
    "entity_kinds=$($ek -join ',')",
    "visual_assets=$va", "visual_asset_statuses=$vs",
    "visual_artifacts=$aa", "artifact_statuses=$as", "artifact_layers=$al", "artifacts_degraded=$ad",
    "foreshadow_1=$f1", "scene_1_title=$t1"
)
$checkFile = Join-Path $dir "fixture-selfcheck.txt"
[System.IO.File]::WriteAllLines($checkFile, $check, (New-Object System.Text.UTF8Encoding($false)))
"自检已写入 $checkFile"
