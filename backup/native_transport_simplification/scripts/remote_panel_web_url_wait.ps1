param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [ValidateRange(1, 60)]
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class PanelWindowNative {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lparam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
}
"@

$sessionId = (Get-Process -Id $PID).SessionId
$panel = Get-Process -Name px_panel -ErrorAction Stop |
    Where-Object { $_.SessionId -eq $sessionId } |
    Select-Object -First 1
if ($null -eq $panel) {
    throw "No px_panel process in interactive session $sessionId"
}

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$lastError = $null
do {
    $handles = New-Object System.Collections.Generic.List[System.IntPtr]
    $callback = [PanelWindowNative+EnumWindowsProc]{
        param([IntPtr]$hwnd, [IntPtr]$lparam)
        [uint32]$pid = 0
        [void][PanelWindowNative]::GetWindowThreadProcessId($hwnd, [ref]$pid)
        if ($pid -eq [uint32]$panel.Id) { $handles.Add($hwnd) }
        return $true
    }
    [void][PanelWindowNative]::EnumWindows($callback, [IntPtr]::Zero)
    foreach ($handle in $handles) {
        [void][PanelWindowNative]::ShowWindowAsync($handle, 9)
        [void][PanelWindowNative]::SetForegroundWindow($handle)
    }
    Start-Sleep -Milliseconds 500
    try {
        & (Join-Path $PSScriptRoot "remote_panel_web_url_probe.ps1") -OutputPath $OutputPath
        return
    } catch {
        $lastError = $_.Exception.Message
    }
} while ((Get-Date) -lt $deadline)

throw "Panel WebClient URL did not become available: $lastError"
