param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][int]$FirstLine,
    [Parameter(Mandatory = $true)][int]$LastLine,
    [Parameter(Mandatory = $true)][string]$Token,
    [Parameter(Mandatory = $true)][byte]$Insert
)
$ErrorActionPreference = 'Stop'

# ⚠️ 纯字节插入：ReadAllBytes / WriteAllBytes，**不经过任何编码器**。
#    上一版 out\_byteswap.ps1 在替换后多执行了一次 `$i++`，把「紧跟标识符的那个字节」
#    一起吞掉了（`draw,` → `cdraw `）。本脚本只做插入，输出游标与输入游标独立推进，
#    不存在这类偏移。
$path = (Resolve-Path $Path).Path
$bytes = [System.IO.File]::ReadAllBytes($path)
$tok = [System.Text.Encoding]::ASCII.GetBytes($Token)
$tokLen = $tok.Length

function IsWordByte([byte]$b) {
    if ($b -ge 0x30 -and $b -le 0x39) { return $true }
    if ($b -ge 0x41 -and $b -le 0x5A) { return $true }
    if ($b -ge 0x61 -and $b -le 0x7A) { return $true }
    if ($b -eq 0x5F) { return $true }
    return $false
}

$out = New-Object System.Collections.Generic.List[byte]
$i = 0
$line = 1
$hits = 0
$len = $bytes.Length

while ($i -lt $len) {
    $b = $bytes[$i]

    $inRange = ($line -ge $FirstLine) -and ($line -le $LastLine)
    $matched = $false
    if ($inRange -and ($i + $tokLen -le $len)) {
        $ok = $true
        for ($k = 0; $k -lt $tokLen; $k++) {
            if ($bytes[$i + $k] -ne $tok[$k]) { $ok = $false; break }
        }
        if ($ok) {
            $prevOk = ($i -eq 0) -or (-not (IsWordByte $bytes[$i - 1]))
            $nextIdx = $i + $tokLen
            $nextOk = ($nextIdx -ge $len) -or (-not (IsWordByte $bytes[$nextIdx]))
            if ($prevOk -and $nextOk) { $matched = $true }
        }
    }

    if ($matched) {
        for ($k = 0; $k -lt $tokLen; $k++) { $out.Add($bytes[$i + $k]) }
        $out.Add($Insert)
        $i = $i + $tokLen
        $hits++
        continue
    }

    $out.Add($b)
    if ($b -eq 0x0A) { $line++ }
    $i++
}

[System.IO.File]::WriteAllBytes($path, $out.ToArray())
Write-Output "inserted 0x$('{0:X2}' -f $Insert) after $hits occurrence(s) of '$Token' on lines $FirstLine..$LastLine"
