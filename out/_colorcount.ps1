# Count exact-pixel occurrences of a few colors in a PNG.
#
# Why this exists (alongside _pixdiff.ps1): _pixdiff tells you HOW MANY pixels
# changed, but not WHICH COLOR they became. That gap is where "the arithmetic
# says it should be X" quietly stays unverified -- the frames are not
# reproducible across runs (48 of 80 differ between two runs of the SAME
# binary), so a plain diff against last round's shots drowns the real signal.
# Counting a specific triple is immune to that: positions move, colors don't.
#
# First used to prove the line.*/fill.* alpha change actually landed:
#   line-subtle over bg-surface = #202734   (old, flattened)  -> 4902 px
#   line-subtle over bg-panel   = #29313E   (new, composited) -> 3987 px
# i.e. the old value collapsed 4902 -> 38 (the 38 being the few places that
# really do sit on bg-surface). See refactor/design-spec.md 2.0.3.
#
# Usage:
#   powershell -File out\_colorcount.ps1 -Paths "a.png,b.png" -Colors "29313E,202734"
#
# ASCII-only on purpose: PowerShell 5.1 reads a BOM-less .ps1 as the system ANSI
# code page, which only bites when the file contains non-ASCII. Keep it ASCII.
param(
    [Parameter(Mandatory = $true)]$Paths,
    [Parameter(Mandatory = $true)]$Colors
)
# NOTE: keep $Colors UNTYPED. With a [string] constraint the split below coerces
# the array back into one space-joined string and ToInt32 then gets
# "202734 29303E ..." -- which fails with a confusing "non-parsable characters".
if ($Paths -is [string]) { $Paths = $Paths -split ',' }
$Colors = @($Colors -split ',')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$want = @()
foreach ($c in $Colors) { $want += [Convert]::ToInt32($c, 16) }

foreach ($p in $Paths) {
    $bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $p).Path)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height
    $d = $bmp.LockBits($rect, 'ReadOnly', 'Format32bppArgb')
    $stride = $d.Stride
    $buf = New-Object byte[] ($stride * $bmp.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $buf, 0, $buf.Length)
    $bmp.UnlockBits($d)
    $hit = New-Object int[] $want.Count
    for ($y = 0; $y -lt $bmp.Height; $y++) {
        $ro = $y * $stride
        for ($x = 0; $x -lt $bmp.Width; $x++) {
            # Format32bppArgb in memory is BGRA (little-endian) -> blue first.
            $o = $ro + $x * 4
            $enc = ([int]$buf[$o + 2] -shl 16) -bor ([int]$buf[$o + 1] -shl 8) -bor [int]$buf[$o]
            for ($i = 0; $i -lt $want.Count; $i++) {
                if ($enc -eq $want[$i]) { $hit[$i]++ }
            }
        }
    }
    $bmp.Dispose()
    $parts = @()
    for ($i = 0; $i -lt $want.Count; $i++) { $parts += ("{0}={1}" -f $Colors[$i], $hit[$i]) }
    Write-Output ("{0,-34} {1}" -f (Split-Path -Leaf $p), ($parts -join '  '))
}
