<#
.SYNOPSIS
Windows-side half of tools/ps2/emu_capture.py: waits for the emulator window,
optionally sends it input, and copies its client area to a PNG.

Separate from the Python driver because finding a window, focusing it, synthesising
input and reading back pixels are all Win32 calls, and the driver is expected to run
under WSL as often as under Windows. It is not meant to be run by hand.
#>
param(
    [Parameter(Mandatory = $true)][string]$ProcessName,
    [int]$WaitWindowSeconds = 30,
    [int]$SettleSeconds = 20,
    [string[]]$Press = @(),
    [int]$PressGapMs = 400,
    [string]$Out = "",
    [int]$AfterPressSeconds = 3,
    [switch]$Kill
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if (-not ([System.Management.Automation.PSTypeName]'EmuWin.Native').Type) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;

namespace EmuWin {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    public static class Native {
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
        [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
        [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
        [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    }
}
"@
}

function Find-EmuWindow {
    param([string]$Name, [int]$TimeoutSeconds)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $proc = Get-Process -Name $Name -ErrorAction SilentlyContinue |
                Where-Object { $_.MainWindowHandle -ne 0 } |
                Select-Object -First 1
        if ($proc) { return $proc }
        Start-Sleep -Milliseconds 500
    }
    return $null
}

$proc = Find-EmuWindow -Name $ProcessName -TimeoutSeconds $WaitWindowSeconds
if (-not $proc) {
    Write-Error "no '$ProcessName' window appeared within $WaitWindowSeconds s"
    exit 2
}
$handle = $proc.MainWindowHandle

# The title needs to reach a steady state before anything is asked of it: a
# capture taken during boot shows a loading screen, and input sent then is lost.
if ($SettleSeconds -gt 0) { Start-Sleep -Seconds $SettleSeconds }

# SW_SHOW (5) rather than SW_RESTORE: a minimised emulator renders nothing to
# capture, but restoring one the user deliberately maximised would be rude.
[void][EmuWin.Native]::ShowWindow($handle, 5)
[void][EmuWin.Native]::SetForegroundWindow($handle)
Start-Sleep -Milliseconds 500

if ($Press.Count -gt 0) {
    foreach ($key in $Press) {
        [System.Windows.Forms.SendKeys]::SendWait($key)
        Start-Sleep -Milliseconds $PressGapMs
    }
    if ($AfterPressSeconds -gt 0) { Start-Sleep -Seconds $AfterPressSeconds }
}

if ($Out -ne "") {
    $rect = New-Object EmuWin.RECT
    [void][EmuWin.Native]::GetClientRect($handle, [ref]$rect)
    $origin = New-Object EmuWin.POINT
    [void][EmuWin.Native]::ClientToScreen($handle, [ref]$origin)

    $width = [int]($rect.Right - $rect.Left)
    $height = [int]($rect.Bottom - $rect.Top)
    if ($width -le 0 -or $height -le 0) {
        Write-Error "emulator window reported a $($width)x$($height) client area"
        exit 3
    }

    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen(
            (New-Object System.Drawing.Point($origin.X, $origin.Y)),
            (New-Object System.Drawing.Point(0, 0)),
            (New-Object System.Drawing.Size($width, $height)))
        $bitmap.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    Write-Output "window ${width}x${height}"
}

if ($Kill) {
    Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

exit 0
