#requires -Version 5.1

[CmdletBinding()]
param(
    [ValidateSet('Show', 'Screenshot', 'Click', 'Type', 'Paste', 'Key', 'Wheel')]
    [string]$Action = 'Screenshot',
    [string]$PanelPath = '',
    [string]$OutputPath = '',
    [int]$X = 0,
    [int]$Y = 0,
    [string]$Text = '',
    [string]$Keys = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PixelsPanelUi {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern short VkKeyScan(char character);
    public static void Scroll(int delta) { mouse_event(0x0800, 0, 0, unchecked((uint)delta), UIntPtr.Zero); }
    public static void TypeText(string text) {
        foreach (char character in text) {
            short mapping = VkKeyScan(character);
            if (mapping == -1) throw new InvalidOperationException("unsupported UI test character");
            byte key = (byte)(mapping & 0xff);
            bool shift = (mapping & 0x0100) != 0;
            if (shift) keybd_event(0x10, 0, 0, UIntPtr.Zero);
            keybd_event(key, 0, 0, UIntPtr.Zero);
            keybd_event(key, 0, 2, UIntPtr.Zero);
            if (shift) keybd_event(0x10, 0, 2, UIntPtr.Zero);
        }
    }
    public struct Rect { public int Left; public int Top; public int Right; public int Bottom; }
}
'@

function Get-PanelWindow {
    $process = Get-Process -Name px_panel -ErrorAction SilentlyContinue | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
    if (-not $process -and $PanelPath) {
        Start-Process -FilePath $PanelPath -WorkingDirectory (Split-Path $PanelPath -Parent)
        $deadline = (Get-Date).AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 250
            $process = Get-Process -Name px_panel -ErrorAction SilentlyContinue | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
        } until ($process -or (Get-Date) -ge $deadline)
    }
    if (-not $process) { throw 'px_panel has no interactive window' }
    [void][PixelsPanelUi]::ShowWindow($process.MainWindowHandle, 9)
    [void][PixelsPanelUi]::SetForegroundWindow($process.MainWindowHandle)
    Start-Sleep -Milliseconds 300
    return $process
}

$panel = Get-PanelWindow
$rect = [PixelsPanelUi+Rect]::new()
if (-not [PixelsPanelUi]::GetWindowRect($panel.MainWindowHandle, [ref]$rect)) { throw 'cannot read px_panel window rectangle' }
$screenX = $rect.Left + $X
$screenY = $rect.Top + $Y

switch ($Action) {
    'Show' {}
    'Screenshot' {
        if (-not $OutputPath) { throw 'OutputPath is required for Screenshot' }
        $width = $rect.Right - $rect.Left
        $height = $rect.Bottom - $rect.Top
        $bitmap = [Drawing.Bitmap]::new($width, $height)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
            $bitmap.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
    'Click' {
        [void][PixelsPanelUi]::SetCursorPos($screenX, $screenY)
        [PixelsPanelUi]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    }
    'Type' {
        [void][PixelsPanelUi]::SetCursorPos($screenX, $screenY)
        [PixelsPanelUi]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x41, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x41, 0, 2, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
        [PixelsPanelUi]::TypeText($Text)
    }
    'Paste' {
        [void][PixelsPanelUi]::SetCursorPos($screenX, $screenY)
        [PixelsPanelUi]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        Set-Clipboard -Value $Text
        [PixelsPanelUi]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x41, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x41, 0, 2, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x56, 0, 0, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x56, 0, 2, [UIntPtr]::Zero)
        [PixelsPanelUi]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    }
    'Key' { [Windows.Forms.SendKeys]::SendWait($Keys) }
    'Wheel' {
        [void][PixelsPanelUi]::SetCursorPos($screenX, $screenY)
        [PixelsPanelUi]::Scroll(-960)
    }
}
