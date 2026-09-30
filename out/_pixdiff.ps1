param(
    [Parameter(Mandatory = $true)][string]$A,
    [Parameter(Mandatory = $true)][string]$B,
    [int]$Toler = 0
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$ia = [System.Drawing.Bitmap]::FromFile((Resolve-Path $A).Path)
$ib = [System.Drawing.Bitmap]::FromFile((Resolve-Path $B).Path)
if ($ia.Width -ne $ib.Width -or $ia.Height -ne $ib.Height) {
    Write-Output "SIZE-DIFF $($ia.Width)x$($ia.Height) vs $($ib.Width)x$($ib.Height)"
    $ia.Dispose(); $ib.Dispose(); exit 2
}
$minX = $ia.Width; $minY = $ia.Height; $maxX = -1; $maxY = -1; $n = 0
$rect = New-Object System.Drawing.Rectangle 0, 0, $ia.Width, $ia.Height
$dataA = $ia.LockBits($rect, 'ReadOnly', 'Format32bppArgb')
$dataB = $ib.LockBits($rect, 'ReadOnly', 'Format32bppArgb')
$stride = $dataA.Stride
$bufA = New-Object byte[] ($stride * $ia.Height)
$bufB = New-Object byte[] ($stride * $ib.Height)
[System.Runtime.InteropServices.Marshal]::Copy($dataA.Scan0, $bufA, 0, $bufA.Length)
[System.Runtime.InteropServices.Marshal]::Copy($dataB.Scan0, $bufB, 0, $bufB.Length)
$ia.UnlockBits($dataA); $ib.UnlockBits($dataB)
for ($y = 0; $y -lt $ia.Height; $y++) {
    $ro = $y * $stride
    for ($x = 0; $x -lt $ia.Width; $x++) {
        $o = $ro + $x * 4
        $d = [Math]::Abs([int]$bufA[$o] - [int]$bufB[$o]) +
             [Math]::Abs([int]$bufA[$o + 1] - [int]$bufB[$o + 1]) +
             [Math]::Abs([int]$bufA[$o + 2] - [int]$bufB[$o + 2])
        if ($d -gt $Toler) {
            $n++
            if ($x -lt $minX) { $minX = $x }
            if ($y -lt $minY) { $minY = $y }
            if ($x -gt $maxX) { $maxX = $x }
            if ($y -gt $maxY) { $maxY = $y }
        }
    }
}
$ia.Dispose(); $ib.Dispose()
Write-Output "diff-pixels=$n"
if ($n -gt 0) {
    Write-Output "bbox=$minX,$minY .. $maxX,$maxY  (w=$($maxX - $minX + 1) h=$($maxY - $minY + 1))"
}
