#requires -Version 7.0

[CmdletBinding()]
param(
    [string]$AppId = 'app-10-ac0adf25',
    [ValidateRange(10, 120)]
    [int]$TimeoutSeconds = 45,
    [switch]$ForceTcp,
    [switch]$ForceRelay,
    [switch]$ExerciseInput,
    [switch]$Rdp,
    [switch]$ExpectRejected
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$consoleBase = 'https://39.71.45.66:4600'
$clientPath = Join-Path $repository 'build_official/dist/px_client.exe'
$credentialsPath = Join-Path $repository '.env/node90_test_user.json'
$licensePath = Join-Path $repository '.env/node90_license.json'
$machinePath = Join-Path $repository '.env/test_machine.md'

function Invoke-ConsoleApi([string]$Path, [object]$Body, [string]$Token = '') {
    $headers = @{ Origin = $consoleBase }
    if ($Token) {
        $headers.Authorization = "Bearer $Token"
    }
    $parameters = @{
        Uri = "$consoleBase$Path"
        Method = 'Post'
        Headers = $headers
        ContentType = 'application/json'
        Body = $Body | ConvertTo-Json -Compress -Depth 12
        SkipCertificateCheck = $true
        NoProxy = $true
        TimeoutSec = 20
    }
    $response = Invoke-RestMethod @parameters
    if ($response.code -ne 200) {
        throw "Console API failed: path=$Path code=$($response.code)"
    }
    return $response.data
}

foreach ($path in @($clientPath, $credentialsPath, $licensePath, $machinePath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Native acceptance input is missing: $path"
    }
}
$buildClient = Join-Path $repository 'build_official/src/px_deps/px_client.exe'
if ((Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $buildClient -Algorithm SHA256).Hash) {
    throw 'The official Client and build-tree artifact hashes differ.'
}

$credentials = Get-Content -LiteralPath $credentialsPath -Raw | ConvertFrom-Json
$license = Get-Content -LiteralPath $licensePath -Raw | ConvertFrom-Json
$machineText = Get-Content -LiteralPath $machinePath -Raw
$nodePassword = [regex]::Match($machineText, '(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
$credential = [pscredential]::new('administrator', (ConvertTo-SecureString $nodePassword -AsPlainText -Force))
$previousTrustedHosts = (Get-Item WSMan:\localhost\Client\TrustedHosts).Value
$session = $null
$instance = $null
$client = $null
$token = ''
$clientLogPath = Join-Path (Split-Path $clientPath -Parent) 'px_logs/px_client.log'
$clientLogOffset = if (Test-Path -LiteralPath $clientLogPath) { (Get-Item -LiteralPath $clientLogPath).Length } else { 0L }
$preferenceSnapshot = Join-Path $env:TEMP "pixels-node90-preferences-$PID-$([guid]::NewGuid().ToString('N'))"
try {
    $login = Invoke-ConsoleApi '/api/v1/session/user/login' @{
        username = $credentials.username
        password = $credentials.password
        client_type = 'panel'
    }
    $token = [string]$login.access_token
    $nonce = [guid]::NewGuid().ToString('N')
    $instance = Invoke-ConsoleApi "/api/v1/user/apps/$AppId/start" @{ client_nonce = $nonce } $token
    if ($instance.state -ne 'running') {
        throw "Node90 instance did not enter running state: $($instance.state)"
    }
    $descriptor = Invoke-ConsoleApi "/api/v1/user/instances/$($instance.instance_id)/native-connection" @{
        view_only = $false
        client_capability = if ($Rdp) { 'windows-rdp-v1' } else { 'pixels-imgui-v1' }
    } $token
    if (-not $descriptor.host -or $descriptor.port -le 0 -or -not $descriptor.device_id -or $descriptor.PSObject.Properties['ticket']) {
        throw 'Native descriptor is invalid or still contains a ticket.'
    }

    Set-Item WSMan:\localhost\Client\TrustedHosts -Value '39.71.45.66' -Force
    $session = New-PSSession -ComputerName '39.71.45.66' -Credential $credential
    $remotePreferences = @(Invoke-Command -Session $session -ScriptBlock {
        Get-ChildItem 'C:\Users\Public\Pixels\px_data\pixels.dat' -Filter '*.ldb' |
            Sort-Object LastWriteTime -Descending |
            Select-Object -ExpandProperty FullName
    })
    [void](New-Item -ItemType Directory -Path $preferenceSnapshot -Force)
    foreach ($remotePreference in $remotePreferences) {
        Copy-Item -FromSession $session -LiteralPath $remotePreference -Destination $preferenceSnapshot -Force
    }
    $levelDbTool = 'C:\source\vcpkg\buildtrees\leveldb\x64-windows-rel\leveldbutil.exe'
    if (-not (Test-Path -LiteralPath $levelDbTool)) {
        throw 'The LevelDB inspection tool required by the test harness is unavailable.'
    }
    Push-Location $preferenceSnapshot
    try {
        $dump = @(Get-ChildItem -LiteralPath $preferenceSnapshot -Filter '*.ldb' | ForEach-Object {
            & $levelDbTool dump $_.Name 2>&1
        }) -join "`n"
    } finally {
        Pop-Location
    }
    if ($dump -notmatch "'device_random_pwd'\s+@\s+\d+\s+:\s+val\s+=>\s+'([^'\r\n]+)'") {
        throw 'The node temporary password was not found in the test preference snapshot.'
    }
    $remotePassword = $Matches[1].Trim()
    $algorithm = [Security.Cryptography.MD5]::Create()
    try {
        $passwordHash = ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($remotePassword)))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
        $remotePassword = $null
        $dump = $null
    }
    if ($ExpectRejected) {
        $passwordHash = '00000000000000000000000000000000'
    }

    $launch = @{
        schema = 1
        host = [string]$descriptor.host
        port = [int]$descriptor.port
        stream_id = [string]$instance.instance_id
        connection_instance_id = [string]$instance.instance_id
        connection_nonce = $nonce
        device_id = "acceptance_$nonce"
        remote_device_id = [string]$descriptor.device_id
        remote_password_hash = [string]$passwordHash
        audio = $true
        clipboard = $true
    }
    $launchArgument = '--native-launch-stdin'
    if ($Rdp) {
        if (-not $descriptor.rdp) {
            throw 'The RDP Native descriptor did not contain protected RDP configuration.'
        }
        $launch.rdp = $descriptor.rdp
        $launch.nonce = $launch.connection_nonce
        $launch.instance_id = $launch.connection_instance_id
        $launchArgument = '--rdp-launch-stdin'
    } else {
        $launch.mode = 'desktop'
        $launch.appkey = [string]$license.appkey
        $launch.stream_name = 'Node90 ImGui acceptance'
        $launch.language = 'zh-CN'
        $launch.decoder = 'Auto'
        $launch.only_viewing = $false
        $launch.force_tcp = [bool]$ForceTcp
        $launch.force_relay = [bool]$ForceRelay
        $launch.split_windows = $false
        $launch.relay_host = [string]$descriptor.relay_host
        $launch.relay_port = [int]$descriptor.relay_port
        $launch.relay_remote_device_id = [string]$descriptor.signal_device_id
    }
    $envelope = $launch | ConvertTo-Json -Compress -Depth 12
    $launch = $null
    $start = [Diagnostics.ProcessStartInfo]::new($clientPath, $launchArgument)
    $start.WorkingDirectory = Split-Path $clientPath -Parent
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $client = [Diagnostics.Process]::Start($start)
    $stdoutDrain = $client.StandardOutput.ReadToEndAsync()
    $stderrDrain = $client.StandardError.ReadToEndAsync()
    $client.StandardInput.Write($envelope)
    $client.StandardInput.Close()
    $envelope = $null

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 250
        $client.Refresh()
        if ($client.HasExited) {
            throw "ImGui Client exited before acceptance: exit=$($client.ExitCode)"
        }
    } while ($client.MainWindowHandle -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($client.MainWindowHandle -eq [IntPtr]::Zero) {
        throw 'ImGui Client did not expose a connected workspace before the deadline.'
    }
    $modules = @($client.Modules | Where-Object { $_.ModuleName -match '^Qt\d' })
    if ($modules.Count -ne 0) {
        throw 'ImGui Client loaded a Qt runtime module.'
    }
    if ($ExerciseInput) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PixelsInputProbe {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
}
'@
        $rect = [PixelsInputProbe+Rect]::new()
        [void][PixelsInputProbe]::GetWindowRect($client.MainWindowHandle, [ref]$rect)
        [void][PixelsInputProbe]::SetForegroundWindow($client.MainWindowHandle)
        $centerX = [int](($rect.Left + $rect.Right) / 2)
        $centerY = [int](($rect.Top + $rect.Bottom) / 2)
        [void][PixelsInputProbe]::SetCursorPos($centerX, $centerY)
        [PixelsInputProbe]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [PixelsInputProbe]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        $launcherX = $rect.Right - 70
        $launcherY = [int](($rect.Top + $rect.Bottom) / 2)
        [void][PixelsInputProbe]::SetCursorPos($launcherX, $launcherY)
        [PixelsInputProbe]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        foreach ($step in 1..8) {
            [void][PixelsInputProbe]::SetCursorPos(
                [int]($launcherX + (120 - $launcherX) * $step / 8),
                [int]($launcherY + (120 - $launcherY) * $step / 8))
            Start-Sleep -Milliseconds 20
        }
        [PixelsInputProbe]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    }
    Start-Sleep -Seconds 2
    $client.Kill()
    [void]$client.WaitForExit(5000)
    $logEvidence = ''
    if (Test-Path -LiteralPath $clientLogPath) {
        $stream = [IO.File]::Open($clientLogPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            [void]$stream.Seek([Math]::Min($clientLogOffset, $stream.Length), [IO.SeekOrigin]::Begin)
            $reader = [IO.StreamReader]::new($stream)
            try { $logEvidence = $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    }
    $evidence = [string]$stdoutDrain.Result + $logEvidence
    $decoderRebuilds = ([regex]::Matches($evidence, 'Rebuild video decoder')).Count
    if (-not $ExpectRejected -and ((-not $Rdp -and $evidence -notmatch 'Video frame came|Video frame stream reset|key frame') -or
            $decoderRebuilds -gt 1)) {
        throw "Video acceptance failed: frame_evidence=$($evidence.Length -gt 0) decoder_rebuilds=$decoderRebuilds"
    }
    [pscustomobject]@{
        Result = 'PASS'
        Mode = if ($ExpectRejected) { 'Rejected password' } elseif ($Rdp) { 'RDP' } elseif ($ForceRelay) { 'WebSocket Relay' } elseif ($ForceTcp) { 'WebSocket' } else { 'UDP/FEC' }
        InstanceId = $instance.instance_id
        Endpoint = "$($descriptor.host):$($descriptor.port)"
        ClientProcessId = $client.Id
        QtModuleCount = $modules.Count
        DecoderRebuildCount = $decoderRebuilds
        ClientHash = (Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash
    }
} finally {
    if ($client -and -not $client.HasExited) {
        $client.Kill()
        [void]$client.WaitForExit(5000)
    }
    if ($instance -and $token) {
        try {
            [void](Invoke-ConsoleApi "/api/v1/user/instances/$($instance.instance_id)/stop" @{ reason = 'native_imgui_acceptance' } $token)
        } catch {
            Write-Warning 'The bounded acceptance instance could not be stopped through Console.'
        }
    }
    if ($session) {
        Remove-PSSession $session
    }
    if ($preferenceSnapshot.StartsWith($env:TEMP, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $preferenceSnapshot -Recurse -Force -ErrorAction SilentlyContinue
    }
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $previousTrustedHosts -Force
}
