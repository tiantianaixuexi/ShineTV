# scripts/crop_zoom.ps1 -- 裁剪并放大截图的一块区域，方便读清小字。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\crop_zoom.ps1 `
#     -In build\_shot.png -Out build\_crop.png -X 1590 -Y 55 -W 390 -H 260 -Scale 4
#
# 注意：**必须是纯 ASCII**（Windows PowerShell 5.1 读无 BOM 的 UTF-8 会把中文变乱码）。
param(
    [Parameter(Mandatory = $true)][string]$In,
    [Parameter(Mandatory = $true)][string]$Out,
    [Parameter(Mandatory = $true)][int]$X,
    [Parameter(Mandatory = $true)][int]$Y,
    [Parameter(Mandatory = $true)][int]$W,
    [Parameter(Mandatory = $true)][int]$H,
    [int]$Scale = 3
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$inPath = (Resolve-Path $In).Path
$outPath = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Out))
$src = [System.Drawing.Image]::FromFile($inPath)
if ($X + $W -gt $src.Width) { $W = $src.Width - $X }
if ($Y + $H -gt $src.Height) { $H = $src.Height - $Y }

$rect = New-Object System.Drawing.Rectangle $X, $Y, $W, $H
$crop = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($crop)
$g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, $W, $H), $rect, [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()

$zw = $W * $Scale
$zh = $H * $Scale
$zoomed = New-Object System.Drawing.Bitmap $zw, $zh
$g2 = [System.Drawing.Graphics]::FromImage($zoomed)
$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g2.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g2.DrawImage($crop, 0, 0, $zw, $zh)
$g2.Dispose()
$zoomed.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)

Write-Output ("crop {0},{1} {2}x{3} x{4} -> {5}" -f $X, $Y, $W, $H, $Scale, $outPath)
$src.Dispose(); $crop.Dispose(); $zoomed.Dispose()
