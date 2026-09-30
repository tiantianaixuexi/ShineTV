param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$From,   # ASCII
    [Parameter(Mandatory = $true)][string]$To,     # ASCII, same length as From
    [string]$NextNot = ''                          # only replace when next byte != this
)
$ErrorActionPreference = 'Stop'

# ⚠️ 纯字节定长替换：ReadAllBytes / WriteAllBytes，**不经过任何编码器**。
#    输出列表与输入游标独立推进（不共用游标），因此不存在「多/少走一字节」的偏移。
#    From 与 To 必须**等长**，这样游标推进量恒等于 1。
$path = (Resolve-Path $Path).Path
$bytes = [System.IO.File]::ReadAllBytes($path)
$fromB = [System.Text.Encoding]::ASCII.GetBytes($From)
$toB = [System.Text.Encoding]::ASCII.GetBytes($To)
if ($fromB.Length -ne $toB.Length) {
    throw "From/To must be the same length ($($fromB.Length) vs $($toB.Length))"
}
$nextNotB = if ($NextNot -eq '') { @() } else { [System.Text.Encoding]::ASCII.GetBytes($NextNot) }
$fl = $fromB.Length
$len = $bytes.Length

$out = New-Object System.Collections.Generic.List[byte]
$i = 0
$hits = 0

while ($i -lt $len) {
    if (($i + $fl) -le $len) {
        $ok = $true
        for ($k = 0; $k -lt $fl; $k++) {
            if ($bytes[$i + $k] -ne $fromB[$k]) { $ok = $false; break }
        }
        if ($ok -and $nextNotB.Length -gt 0) {
            $n = $i + $fl
            if ($n -lt $len -and $bytes[$n] -eq $nextNotB[0]) { $ok = $false }
        }
        if ($ok) {
            for ($k = 0; $k -lt $fl; $k++) { $out.Add($toB[$k]) }
            $i = $i + $fl
            $hits++
            continue
        }
    }
    $out.Add($bytes[$i])
    $i++
}

[System.IO.File]::WriteAllBytes($path, $out.ToArray())
Write-Output "replaced $hits occurrence(s) of '$From' -> '$To'"
