#requires -Version 7.0

[CmdletBinding()]
param(
    [string]$ConsoleBase = 'https://39.71.45.66:4600',
    [string]$DeviceId = '',
    [ValidateRange(15, 180)]
    [int]$ClientTimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$clientPath = Join-Path $repository 'build_official/dist/client/px_client.exe'
$buildClientPath = Join-Path $repository 'build_official/client/src/px_deps/px_client.exe'
$credentialsPath = Join-Path $repository '.env/public_test_user.json'
$licensePath = Join-Path $repository '.env/public_license.json'
$clientLogPath = Join-Path (Split-Path $clientPath -Parent) 'px_logs/px_client.log'

foreach ($path in @($clientPath, $buildClientPath, $credentialsPath, $licensePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Windows desktop acceptance input is missing: $path"
    }
}
if ((Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $buildClientPath -Algorithm SHA256).Hash) {
    throw 'The Client product build and dist artifact hashes differ.'
}

$credentials = Get-Content -LiteralPath $credentialsPath -Raw | ConvertFrom-Json
$license = Get-Content -LiteralPath $licensePath -Raw | ConvertFrom-Json
$token = ''
$client = $null
$clientLogOffset = if (Test-Path -LiteralPath $clientLogPath) { (Get-Item -LiteralPath $clientLogPath).Length } else { 0L }

function Invoke-ConsoleApi {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [ValidateSet('GET', 'POST')]
        [string]$Method = 'GET',
        [object]$Body = $null,
        [string]$AccessToken = ''
    )

    $headers = @{ Accept = 'application/json'; Origin = $ConsoleBase }
    if ($AccessToken) {
        $headers.Authorization = "Bearer $AccessToken"
    }
    $parameters = @{
        Uri = "$ConsoleBase$Path"
        Method = $Method
        Headers = $headers
        SkipCertificateCheck = $true
        NoProxy = $true
        TimeoutSec = 20
    }
    if ($null -ne $Body) {
        $parameters.ContentType = 'application/json'
        $parameters.Body = $Body | ConvertTo-Json -Compress -Depth 12
    }
    $response = Invoke-RestMethod @parameters
    if ($response.code -ne 200) {
        throw "Console API failed: path=$Path code=$($response.code)"
    }
    return $response.data
}

function Read-NewClientLog {
    if (-not (Test-Path -LiteralPath $clientLogPath)) {
        return ''
    }
    $stream = [IO.File]::Open(
        $clientLogPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite)
    try {
        [void]$stream.Seek([Math]::Min($clientLogOffset, $stream.Length), [IO.SeekOrigin]::Begin)
        $reader = [IO.StreamReader]::new($stream)
        try {
            return $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

try {
    $login = Invoke-ConsoleApi -Path '/api/v1/session/user/login' -Method POST -Body @{
        username = $credentials.username
        password = $credentials.password
        client_type = 'panel'
    }
    $token = [string]$login.access_token
    if (-not $token) {
        throw 'Windows Panel login returned no access token.'
    }

    $devices = @(Invoke-ConsoleApi -Path '/api/v1/user/devices' -AccessToken $token)
    $device = if ($DeviceId) {
        $devices | Where-Object { $_.device_id -eq $DeviceId -and $_.online } | Select-Object -First 1
    } else {
        $devices | Where-Object { $_.online -and $_.platform -eq 'windows' } | Select-Object -First 1
    }
    if (-not $device) {
        throw 'No matching online Windows desktop is available to the test user.'
    }

    $encodedDeviceId = [Uri]::EscapeDataString([string]$device.device_id)
    $descriptor = Invoke-ConsoleApi -Path "/api/v1/user/devices/$encodedDeviceId/native-connection" -Method POST -Body @{} -AccessToken $token
    if (-not $descriptor.host -or -not $descriptor.device_id -or -not $descriptor.password_hash -or
        [int]$descriptor.port -le 0 -or [int]$descriptor.port -gt 65535) {
        throw 'Console returned an invalid current desktop connection descriptor.'
    }
    if ($descriptor.PSObject.Properties['ticket']) {
        throw 'Console returned a retired desktop connection field.'
    }
    if (-not (Test-NetConnection -ComputerName ([string]$descriptor.host) -Port ([int]$descriptor.port) -InformationLevel Quiet)) {
        throw 'The Console-provided desktop TCP endpoint is unreachable.'
    }

    $nonce = [guid]::NewGuid().ToString('N')
    $launch = @{
        schema = 1
        host = [string]$descriptor.host
        port = [int]$descriptor.port
        stream_id = "direct-$nonce"
        connection_instance_id = "direct-$nonce"
        connection_nonce = $nonce
        device_id = "desktop_acceptance_$nonce"
        remote_device_id = [string]$descriptor.device_id
        remote_password_hash = [string]$descriptor.password_hash
        mode = 'desktop'
        appkey = [string]$license.appkey
        stream_name = 'Windows public desktop acceptance'
        language = 'zh-CN'
        decoder = 'Auto'
        audio = $true
        clipboard = $true
        only_viewing = $false
        force_tcp = $false
        force_relay = $false
        split_windows = $false
        relay_host = [string]$descriptor.relay_host
        relay_port = [int]$descriptor.relay_port
        relay_remote_device_id = [string]$descriptor.signal_device_id
    }
    $startInfo = [Diagnostics.ProcessStartInfo]::new($clientPath, '--native-launch-stdin')
    $startInfo.WorkingDirectory = Split-Path $clientPath -Parent
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $client = [Diagnostics.Process]::Start($startInfo)
    $stdoutDrain = $client.StandardOutput.ReadToEndAsync()
    $stderrDrain = $client.StandardError.ReadToEndAsync()
    $client.StandardInput.Write(($launch | ConvertTo-Json -Compress -Depth 12))
    $client.StandardInput.Close()
    $launch = $null

    $deadline = [DateTime]::UtcNow.AddSeconds($ClientTimeoutSeconds)
    $frameReady = $false
    do {
        Start-Sleep -Milliseconds 500
        $client.Refresh()
        if ($client.HasExited) {
            throw "Windows desktop Client exited before acceptance completed: exit=$($client.ExitCode)"
        }
        $evidence = Read-NewClientLog
        $frameReady = $evidence -match 'Video frame came|Video frame stream reset|key frame|\[LAT-decode\].*frames=[1-9][0-9]*'
    } while ((-not $frameReady -or $client.MainWindowHandle -eq [IntPtr]::Zero) -and [DateTime]::UtcNow -lt $deadline)

    if (-not $frameReady) {
        throw 'Windows desktop Client did not decode a video frame before the deadline.'
    }
    if ($client.MainWindowHandle -eq [IntPtr]::Zero) {
        throw 'Windows desktop Client did not expose a workspace window before the deadline.'
    }
    $qtModules = @($client.Modules | Where-Object { $_.ModuleName -match '^Qt\d' })
    if ($qtModules.Count -ne 0) {
        throw "Windows desktop Client loaded $($qtModules.Count) Qt runtime modules."
    }

    [pscustomobject]@{
        Result = 'PASS'
        Mode = 'Desktop Native'
        Login = $true
        DeviceId = [string]$device.device_id
        DeviceName = [string]$device.name
        Endpoint = "$($descriptor.host):$($descriptor.port)"
        TcpReachable = $true
        WorkspaceReady = $true
        DecodedFrame = $frameReady
        QtModuleCount = $qtModules.Count
        ClientHash = (Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash
    }
} finally {
    if ($client -and -not $client.HasExited) {
        $client.Kill()
        [void]$client.WaitForExit(5000)
    }
    if ($token) {
        try {
            [void](Invoke-ConsoleApi -Path '/api/v1/session/user/logout' -Method POST -Body @{} -AccessToken $token)
        } catch {
            Write-Warning "Windows desktop test login cleanup failed: $($_.Exception.Message)"
        }
    }
}
