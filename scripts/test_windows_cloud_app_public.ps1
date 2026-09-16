#requires -Version 7.0

[CmdletBinding()]
param(
    [string]$ConsoleBase = 'https://39.71.45.66:4600',
    [string]$AppId = '',
    [switch]$Rdp,
    [ValidateRange(15, 180)]
    [int]$StartTimeoutSeconds = 90,
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
        throw "Windows public acceptance input is missing: $path"
    }
}
if ((Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $buildClientPath -Algorithm SHA256).Hash) {
    throw 'The Client product build and dist artifact hashes differ.'
}

$credentials = Get-Content -LiteralPath $credentialsPath -Raw | ConvertFrom-Json
$license = Get-Content -LiteralPath $licensePath -Raw | ConvertFrom-Json
$token = ''
$instance = $null
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
    try {
        $response = Invoke-RestMethod @parameters
    } catch {
        throw "Console API request failed: method=$Method path=$Path; $($_.Exception.Message)"
    }
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

    $applications = @(Invoke-ConsoleApi -Path '/api/v1/user/apps' -AccessToken $token)
    $application = if ($AppId) {
        $applications | Where-Object { $_.app_id -eq $AppId } | Select-Object -First 1
    } elseif ($Rdp) {
        $applications | Where-Object { $_.app_type -eq 'rdp' } | Select-Object -First 1
    } else {
        $applications | Where-Object { $_.app_type -eq 'webview' } | Select-Object -First 1
    }
    if (-not $application -and -not $AppId) {
        $application = $applications | Where-Object { $_.app_type -eq 'game-hook' } | Select-Object -First 1
    }
    if (-not $application) {
        throw 'No matching Windows cloud application is available to the test user.'
    }

    $nonce = [guid]::NewGuid().ToString('N')
    $encodedAppId = [Uri]::EscapeDataString([string]$application.app_id)
    $instance = Invoke-ConsoleApi -Path "/api/v1/user/apps/$encodedAppId/start" -Method POST -Body @{
        client_nonce = $nonce
    } -AccessToken $token

    $startDeadline = [DateTime]::UtcNow.AddSeconds($StartTimeoutSeconds)
    while ($instance.state -notin @('running', 'failed', 'stopped') -and [DateTime]::UtcNow -lt $startDeadline) {
        Start-Sleep -Milliseconds 500
        $instances = @(Invoke-ConsoleApi -Path '/api/v1/user/instances' -AccessToken $token)
        $instance = $instances | Where-Object { $_.instance_id -eq $instance.instance_id } | Select-Object -First 1
        if (-not $instance) {
            throw 'The newly started cloud application disappeared from the user instance list.'
        }
    }
    if ($instance.state -ne 'running') {
        throw "Cloud application did not enter running state: $($instance.state)"
    }

    $encodedInstanceId = [Uri]::EscapeDataString([string]$instance.instance_id)
    $descriptor = Invoke-ConsoleApi -Path "/api/v1/user/instances/$encodedInstanceId/native-connection" -Method POST -Body @{
        view_only = $false
        client_capability = if ($Rdp) { 'windows-rdp-v1' } else { 'pixels-imgui-v1' }
    } -AccessToken $token
    if (-not $descriptor.host -or -not $descriptor.device_id -or -not $descriptor.password_hash -or
        [int]$descriptor.port -lt 4613 -or [int]$descriptor.port -gt 4998) {
        throw 'Console returned an invalid current Native connection descriptor.'
    }
    if ($descriptor.PSObject.Properties['ticket']) {
        throw 'Console returned a retired connection ticket in the Native descriptor.'
    }
    if (-not (Test-NetConnection -ComputerName ([string]$descriptor.host) -Port ([int]$descriptor.port) -InformationLevel Quiet)) {
        throw 'The dynamic Render TCP endpoint is unreachable.'
    }

    $launch = @{
        schema = 1
        host = [string]$descriptor.host
        port = [int]$descriptor.port
        stream_id = [string]$instance.instance_id
        connection_instance_id = [string]$instance.instance_id
        connection_nonce = $nonce
        device_id = "windows_acceptance_$nonce"
        remote_device_id = [string]$descriptor.device_id
        remote_password_hash = [string]$descriptor.password_hash
        mode = 'desktop'
        appkey = [string]$license.appkey
        stream_name = 'Windows public cloud application acceptance'
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
    $launchArgument = '--native-launch-stdin'
    if ($Rdp) {
        if (-not $descriptor.rdp) {
            throw 'The RDP Native descriptor did not contain protected RDP configuration.'
        }
        $launch.rdp = $descriptor.rdp
        $launch.nonce = $launch.connection_nonce
        $launch.instance_id = $launch.connection_instance_id
        $launchArgument = '--rdp-launch-stdin'
    }
    $startInfo = [Diagnostics.ProcessStartInfo]::new($clientPath, $launchArgument)
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

    $clientDeadline = [DateTime]::UtcNow.AddSeconds($ClientTimeoutSeconds)
    $frameReady = $false
    $fileTransportReady = $false
    do {
        Start-Sleep -Milliseconds 500
        $client.Refresh()
        if ($client.HasExited) {
            throw "Windows Client exited before acceptance completed: exit=$($client.ExitCode)"
        }
        $evidence = Read-NewClientLog
        $frameReady = if ($Rdp) {
            $evidence -match 'event=rdp\.frame\.progress published=[1-9][0-9]*'
        } else {
            $evidence -match 'Video frame came|Video frame stream reset|key frame'
        }
        $fileTransportReady = -not $Rdp -and $evidence -match 'File transfer connected|file transport|ft_data_channel|FT channel|file channel'
    } while ((-not $frameReady -or $client.MainWindowHandle -eq [IntPtr]::Zero) -and [DateTime]::UtcNow -lt $clientDeadline)

    if (-not $frameReady) {
        throw 'Windows Client did not decode a video frame before the deadline.'
    }
    if ($client.MainWindowHandle -eq [IntPtr]::Zero) {
        throw 'Windows Client did not expose a workspace window before the deadline.'
    }
    $qtModules = @($client.Modules | Where-Object { $_.ModuleName -match '^Qt\d' })
    if ($qtModules.Count -ne 0) {
        throw "Windows Client loaded $($qtModules.Count) Qt runtime modules."
    }

    [pscustomobject]@{
        Result = 'PASS'
        Mode = if ($Rdp) { 'RDP' } else { 'Native' }
        Login = $true
        AppId = [string]$application.app_id
        AppType = [string]$application.app_type
        InstanceState = [string]$instance.state
        Endpoint = "$($descriptor.host):$($descriptor.port)"
        DynamicPortValid = $true
        TcpReachable = $true
        WorkspaceReady = $true
        DecodedFrame = $frameReady
        FileTransportEvidence = $fileTransportReady
        QtModuleCount = $qtModules.Count
        ClientHash = (Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash
    }
} finally {
    if ($client -and -not $client.HasExited) {
        $client.Kill()
        [void]$client.WaitForExit(5000)
    }
    if ($instance -and $token) {
        try {
            $encodedInstanceId = [Uri]::EscapeDataString([string]$instance.instance_id)
            [void](Invoke-ConsoleApi -Path "/api/v1/user/instances/$encodedInstanceId/stop" -Method POST -Body @{
                reason = 'windows_public_acceptance'
            } -AccessToken $token)
        } catch {
            Write-Warning "Cloud application cleanup failed: $($_.Exception.Message)"
        }
    }
    if ($token) {
        try {
            [void](Invoke-ConsoleApi -Path '/api/v1/session/user/logout' -Method POST -Body @{} -AccessToken $token)
        } catch {
            Write-Warning "Windows test login cleanup failed: $($_.Exception.Message)"
        }
    }
}
