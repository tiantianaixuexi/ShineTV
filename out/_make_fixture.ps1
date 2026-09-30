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
# 下面两行是**编码自检**：中文正常就说明没被 GBK 码页重编码过。
$f1 = & $sqlite $Db 'select title from foreshadowings where id=1;'
$t1 = & $sqlite $Db 'select title from scenes where id=1;'

"entities          = $e"
"scenes            = $sc"
"shots             = $sh"
"open_foreshadows  = $of"
"scene_foreshadows = $sf"
"entity kinds      = $($ek -join ', ')"
"foreshadow#1      = $f1"
"scene#1 title     = $t1"

# 硬门禁：数量不对就直接失败，别让一个残缺的 fixture 跑完 40 多张取证图。
if ("$e/$sc/$sh/$of/$sf" -ne "3/3/7/2/3") { throw "fixture 行数不对：$e/$sc/$sh/$of/$sf（期望 3/3/7/2/3）" }

# 自检同时落一份盘。控制台回显在本机会零星吞掉整行（`entities` / `foreshadow#1`
# 两行稳定显示为空，但门禁比字符串证明它们的值是对的）—— 门禁过了不等于**看得见**，
# 所以把同一份数字写进文件，取证报告直接引这个文件，不引控制台。
$check = @(
    "entities=$e", "scenes=$sc", "shots=$sh",
    "open_foreshadows=$of", "scene_foreshadows=$sf",
    "entity_kinds=$($ek -join ',')",
    "foreshadow_1=$f1", "scene_1_title=$t1"
)
$checkFile = Join-Path $dir "fixture-selfcheck.txt"
[System.IO.File]::WriteAllLines($checkFile, $check, (New-Object System.Text.UTF8Encoding($false)))
"自检已写入 $checkFile"
