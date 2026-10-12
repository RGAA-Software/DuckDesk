param([Parameter(Mandatory)][int]$ClientProcessId, [Parameter(Mandatory)][string]$Workspace)
$ErrorActionPreference = 'Stop'
$client = Get-Process -Id $ClientProcessId
if ($client.Path -ne 'C:\Program Files\Pixels Client\px_client.exe') { throw 'Foreign Client' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class OwnedContentDrag {
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int Horizontal; public int Vertical; }
    [StructLayout(LayoutKind.Sequential)] public struct Rectangle { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rectangle bounds);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int horizontal, int vertical);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint horizontal, uint vertical, uint buttons, UIntPtr extra);
}
'@
$window = $client.MainWindowHandle
if ($window -eq [IntPtr]::Zero) { throw 'Owned window unavailable' }
$previousDpi = [OwnedContentDrag]::SetThreadDpiAwarenessContext([IntPtr](-4))
$previousForeground = [OwnedContentDrag]::GetForegroundWindow()
$originalCursor = [OwnedContentDrag+Point]::new()
$bounds = [OwnedContentDrag+Rectangle]::new()
if (-not [OwnedContentDrag]::GetCursorPos([ref]$originalCursor) -or -not [OwnedContentDrag]::GetWindowRect($window,[ref]$bounds)) { throw 'Native geometry unavailable' }
$centerHorizontal = [int]($bounds.Left + ($bounds.Right-$bounds.Left)*0.65)
$centerVertical = [int]($bounds.Top + ($bounds.Bottom-$bounds.Top)*0.65)
$horizontalRadius = [int](($bounds.Right-$bounds.Left)*0.12)
$verticalRadius = [int](($bounds.Bottom-$bounds.Top)*0.10)
$started = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()/1000.0
$positionsAccepted = 0
$leftPressed = $false
try {
    [OwnedContentDrag]::SetForegroundWindow($window) | Out-Null
    Start-Sleep -Milliseconds 200
    if ([OwnedContentDrag]::GetForegroundWindow() -ne $window) { throw 'Owned window did not acquire focus' }
    [OwnedContentDrag]::SetCursorPos($centerHorizontal,$centerVertical) | Out-Null
    [OwnedContentDrag]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
    $leftPressed = $true
    for ($positionIndex=0; $positionIndex -lt 480; $positionIndex++) {
        if ([OwnedContentDrag]::GetForegroundWindow() -ne $window) { throw 'User focus changed; stop owned drag' }
        $horizontal = $centerHorizontal + [int]($horizontalRadius*[Math]::Sin($positionIndex/4.0))
        $vertical = $centerVertical + [int]($verticalRadius*[Math]::Cos($positionIndex/5.0))
        if ([OwnedContentDrag]::SetCursorPos($horizontal,$vertical)) { $positionsAccepted++ }
        Start-Sleep -Milliseconds 1
    }
} finally {
    if ($leftPressed) { [OwnedContentDrag]::mouse_event(4,0,0,0,[UIntPtr]::Zero) }
    [OwnedContentDrag]::SetCursorPos($originalCursor.Horizontal,$originalCursor.Vertical) | Out-Null
    if ([OwnedContentDrag]::GetForegroundWindow() -eq $window) { [OwnedContentDrag]::SetForegroundWindow($previousForeground) | Out-Null }
    [OwnedContentDrag]::SetThreadDpiAwarenessContext($previousDpi) | Out-Null
}
$report = @{client_pid=$client.Id;positions_accepted=$positionsAccepted;started_epoch=$started;ended_epoch=[DateTimeOffset]::Now.ToUnixTimeMilliseconds()/1000.0;method='480 real OS cursor positions with left button held in owned video content; cursor/focus/DPI restored'}
[IO.File]::WriteAllText((Join-Path $Workspace 'pointer-result.json'),($report|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
Write-Output ($report|ConvertTo-Json)
