param(
    [Parameter(Mandatory = $true)][string]$In,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$X, [int]$Y, [int]$W, [int]$H,
    [double]$Scale = 3.0
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $In).Path)
$dw = [int]($W * $Scale); $dh = [int]($H * $Scale)
$dst = New-Object System.Drawing.Bitmap $dw, $dh
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = 'NearestNeighbor'
$g.PixelOffsetMode = 'Half'
$g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, $dw, $dh),
             (New-Object System.Drawing.Rectangle $X, $Y, $W, $H), 'Pixel')
$g.Dispose()
$dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$dst.Dispose(); $src.Dispose()
Write-Output "cropped $W x $H @ ($X,$Y) x$Scale -> $Out"
