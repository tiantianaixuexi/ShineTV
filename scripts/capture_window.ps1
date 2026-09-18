# scripts/capture_window.ps1 -- 启动 ShineTVStudio 并截取主窗口，然后正常关窗。
#
# 用途：让 AI 自己"看"界面做视觉验收（读 PNG 即可），不必每次都请用户肉眼回归。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capture_window.ps1 -Out build\_shot.png
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\crop_zoom.ps1 -In build\_shot.png -X 1590 -Y 55 -W 390 -H 260 -Out build\_crop.png
#
# 三个必须踩过的坑（改脚本前先看）：
#   1) **DX11 交换链用 PrintWindow 只会得到标题栏、客户区全黑** -> 只能置前 + CopyFromScreen。
#   2) `SetForegroundWindow` 对非前台进程会被系统拒绝 -> 先 `HWND_TOPMOST` 抬到最前，
#      再用 AttachThreadInput 把当前线程挂到前台线程上，`SetForegroundWindow` 才会生效。
#   3) 进程窗口默认在 (125,125) 且 2000x1125，可能超出屏幕 -> 挪到 (0,0) 并把尺寸裁到屏幕内。
#   另外：**本文件必须是纯 ASCII**，Windows PowerShell 5.1 读无 BOM 的 UTF-8 脚本会把中文变乱码并报语法错。
param(
    [string]$Out = "build\_shot.png",
    [string]$Exe = "build\ShineTVStudio.exe",
    [string]$WindowTitle = "ShineTV Studio",
    [int]$WaitSec = 7,
    [int]$SettleMs = 2000,
    # Optional: park the real cursor at (X,Y) inside the (now 0,0-positioned) main window before
    # capturing, so hover-only UI (tooltips / hovers) shows up in the shot. -1 = do not move.
    [int]$CursorX = -1,
    [int]$CursorY = -1,
    [int]$CursorSettleMs = 1200
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Capture {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint attach, uint attachTo, bool fAttach);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@

[void][Win32Capture]::SetProcessDPIAware()
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds

$exePath = (Resolve-Path $Exe).Path
$proc = Start-Process -FilePath $exePath -PassThru
Start-Sleep -Seconds $WaitSec
$proc.Refresh()

# Pick the window by TITLE (main.cpp sets "ShineTV Studio"), not MainWindowHandle:
# ImGui floating/viewport windows or other desktop windows can make MainWindowHandle point at the
# wrong HWND -> the shot shows the wrong window and CloseMainWindow closes the wrong thing.
$hwnd = [Win32Capture]::FindWindow($null, $WindowTitle)
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Output ("WARN: window titled '{0}' not found; falling back to MainWindowHandle" -f $WindowTitle)
    $hwnd = $proc.MainWindowHandle
}
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Output "FAIL: no main window (window did not appear)"
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    exit 1
}
Write-Output ("PID={0} hwnd={1} title='{2}'" -f $proc.Id, $hwnd, $proc.MainWindowTitle)

$rect = New-Object Win32Capture+RECT
[void][Win32Capture]::GetWindowRect($hwnd, [ref]$rect)
$width = [Math]::Min($rect.Right - $rect.Left, $screen.Width)
$height = [Math]::Min($rect.Bottom - $rect.Top, $screen.Height)
Write-Output ("screen {0}x{1}; window {2}x{3} -> capture {4}x{5} at 0,0" -f `
    $screen.Width, $screen.Height, ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top), $width, $height)

# 抬到最前 + 挪到左上角并裁到屏幕内
[void][Win32Capture]::ShowWindow($hwnd, 9)                                  # SW_RESTORE
[void][Win32Capture]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, $width, $height, 0x0040)  # HWND_TOPMOST | SWP_SHOWWINDOW
$fgThread = [Win32Capture]::GetWindowThreadProcessId([Win32Capture]::GetForegroundWindow(), [IntPtr]::Zero)
[void][Win32Capture]::AttachThreadInput($fgThread, [Win32Capture]::GetCurrentThreadId(), $true)
[void][Win32Capture]::SetForegroundWindow($hwnd)
[void][Win32Capture]::AttachThreadInput($fgThread, [Win32Capture]::GetCurrentThreadId(), $false)
Start-Sleep -Milliseconds $SettleMs

# Hover-only UI (tooltips) only shows if the real cursor sits on the item -> move it first.
if ($CursorX -ge 0 -and $CursorY -ge 0) {
    [void][Win32Capture]::SetCursorPos($CursorX, $CursorY)
    Write-Output ("cursor moved to {0},{1}" -f $CursorX, $CursorY)
    Start-Sleep -Milliseconds $CursorSettleMs
}

$fg = [Win32Capture]::GetForegroundWindow()
Write-Output ("foreground ours={0}" -f ($fg -eq $hwnd))

$outPath = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Out))
$bmp = New-Object System.Drawing.Bitmap $width, $height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size($width, $height)))
$g.Dispose()
$bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)

$sum = 0.0; $n = 0
for ($y = 0; $y -lt $bmp.Height; $y += 4) {
    for ($x = 0; $x -lt $bmp.Width; $x += 4) {
        $c = $bmp.GetPixel($x, $y); $sum += ($c.R + $c.G + $c.B) / 3.0; $n++
    }
}
$mean = $sum / [Math]::Max(1, $n)
Write-Output ("saved {0} ({1}x{2}) mean={3:N2}" -f $outPath, $bmp.Width, $bmp.Height, $mean)
$bmp.Dispose()

[void][Win32Capture]::SetWindowPos($hwnd, [IntPtr](-2), 0, 0, 0, 0, 0x0043)  # HWND_NOTOPMOST

# 关窗：CloseMainWindow 走 WM_CLOSE，进程自己正常退出（不会留"删不掉的尸体"）
[void]$proc.CloseMainWindow()
for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Milliseconds 300
    $proc.Refresh()
    if ($proc.HasExited) { break }
}
if (-not $proc.HasExited) {
    Write-Output "CloseMainWindow did not exit in time -> Stop-Process -Force"
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    $proc.Refresh()
}
Write-Output ("HasExited={0} mean={1:N2}" -f $proc.HasExited, $mean)
if ($mean -lt 3) { Write-Output "WARN: image looks black - capture may have failed" }
exit 0
