param([Parameter(Mandatory=$true)][string]$OutputDirectory,[int]$GameProcessId=0,[switch]$WakeDisplay)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing,System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CaptureDpi {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
    [DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint flags);
}
'@
[void][CaptureDpi]::SetProcessDpiAwarenessContext([IntPtr](-4))
if($WakeDisplay){
    # One-shot activity notification only; do not change the machine's power plan.
    $previous=[CaptureDpi]::SetThreadExecutionState(3)
    "wake_display previous=$previous timestamp=$(Get-Date -Format o)" | Out-File (Join-Path $OutputDirectory 'wake.log') -Encoding utf8
    Start-Sleep -Seconds 3
}
if($GameProcessId -gt 0){
    $game=Get-Process -Id $GameProcessId
    if($game.Path -notin @('D:\software\2dadventure\2dAdventure.exe','D:\software\心脏医学可执行\心脏医学可执行\心脏-医学动态展示.exe')){throw 'Unexpected test process'}
    $game|Select-Object Id,SessionId,MainWindowHandle,MainWindowTitle|ConvertTo-Json|Out-File (Join-Path $OutputDirectory 'window.json') -Encoding utf8
    [void][CaptureDpi]::ShowWindow($game.MainWindowHandle,9)
    [void][CaptureDpi]::SetForegroundWindow($game.MainWindowHandle)
    [void][CaptureDpi]::SetCursorPos(900,500)
    Start-Sleep -Seconds 1
}
$bounds=[Windows.Forms.SystemInformation]::VirtualScreen
$image=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height)
$graphics=[Drawing.Graphics]::FromImage($image)
try {$graphics.CopyFromScreen($bounds.Location,[Drawing.Point]::Empty,$bounds.Size);$image.Save((Join-Path $OutputDirectory 'desktop.png'))}
finally{$graphics.Dispose();$image.Dispose()}
