#requires -Version 7.0

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({
        $candidate = $null
        [Uri]::TryCreate($_, [UriKind]::Absolute, [ref]$candidate) -and
            $candidate.Scheme -eq 'https' -and
            -not $candidate.Query -and
            -not $candidate.Fragment
    })]
    [string]$ConsoleBase,
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$CertificateAuthority,
    [string]$AppId = '',
    [switch]$Rdp,
    [switch]$ForceRelay,
    [string]$RelayHost = '',
    [ValidateRange(0, 65535)]
    [int]$RelayPort = 0,
    [switch]$ExerciseInput,
    [ValidateRange(15, 180)]
    [int]$StartTimeoutSeconds = 90,
    [ValidateRange(15, 180)]
    [int]$ClientTimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
$ConsoleBase = $ConsoleBase.TrimEnd('/')
$repository = Split-Path $PSScriptRoot -Parent
$clientPath = Join-Path $repository 'build_official/client/dist/px_client.exe'
$buildClientPath = Join-Path $repository 'build_official/client/cmake/src/px_deps/px_client.exe'
$credentialsPath = Join-Path $repository '.env/public_test_user.json'
$licensePath = Join-Path $repository '.env/public_license.json'
$clientLogPath = Join-Path (Split-Path $clientPath -Parent) 'px_logs/px_client.log'
$trustedRoot = [Security.Cryptography.X509Certificates.X509Certificate2]::CreateFromPem(
    [IO.File]::ReadAllText((Resolve-Path -LiteralPath $CertificateAuthority).Path))
$certificatePolicy = [Security.Cryptography.X509Certificates.X509ChainPolicy]::new()
$certificatePolicy.TrustMode =
    [Security.Cryptography.X509Certificates.X509ChainTrustMode]::CustomRootTrust
[void]$certificatePolicy.CustomTrustStore.Add($trustedRoot)
$certificatePolicy.RevocationMode =
    [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
$certificatePolicy.VerificationFlags =
    [Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
$httpHandler = [Net.Http.SocketsHttpHandler]::new()
$httpHandler.SslOptions.CertificateChainPolicy = $certificatePolicy
$httpClient = [Net.Http.HttpClient]::new($httpHandler)
$httpClient.Timeout = [TimeSpan]::FromSeconds(20)

if ($Rdp) {
    throw 'The PostgreSQL RDP descriptor does not yet carry the protected workspace bootstrap. RDP cannot be accepted with a fabricated or legacy password.'
}
foreach ($path in @($clientPath, $buildClientPath, $credentialsPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Windows public acceptance input is missing: $path"
    }
}
if ($ForceRelay -and ((-not $RelayHost) -or $RelayPort -eq 0 -or -not (Test-Path -LiteralPath $licensePath -PathType Leaf))) {
    throw 'Forced Relay acceptance requires an explicit Relay host, port and public license input.'
}
if ((Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $buildClientPath -Algorithm SHA256).Hash) {
    throw 'The Client product build and dist artifact hashes differ.'
}

$credentials = Get-Content -LiteralPath $credentialsPath -Raw | ConvertFrom-Json
$relayAppKey = if ($ForceRelay) {
    [string](Get-Content -LiteralPath $licensePath -Raw | ConvertFrom-Json).appkey
} else {
    ''
}
$accessToken = ''
$applicationInstance = $null
$resourceSession = $null
$clientProcess = $null
$clientLogOffset = if (Test-Path -LiteralPath $clientLogPath) { (Get-Item -LiteralPath $clientLogPath).Length } else { 0L }
$relayBaseline = $null

function Invoke-ConsoleApi {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [ValidateSet('GET', 'POST', 'DELETE')]
        [string]$Method = 'GET',
        [object]$Body = $null,
        [string]$Token = '',
        [switch]$UserResource
    )

    $request = [Net.Http.HttpRequestMessage]::new(
        [Net.Http.HttpMethod]::new($Method),
        "$ConsoleBase$Path")
    try {
        [void]$request.Headers.TryAddWithoutValidation('Accept', 'application/json')
        [void]$request.Headers.TryAddWithoutValidation(
            'Origin',
            ([Uri]$ConsoleBase).GetLeftPart([UriPartial]::Authority))
        [void]$request.Headers.TryAddWithoutValidation('X-Pixels-Client-Type', 'panel')
        if ($Token) {
            $request.Headers.Authorization = [Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $Token)
        }
        if ($UserResource) {
            [void]$request.Headers.TryAddWithoutValidation('X-Pixels-Subject-Kind', 'user')
        }
        if ($null -ne $Body) {
            $request.Content = [Net.Http.StringContent]::new(
                ($Body | ConvertTo-Json -Compress -Depth 12),
                [Text.Encoding]::UTF8,
                'application/json')
        }
        $response = $httpClient.Send($request)
        try {
            $content = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            if (-not $response.IsSuccessStatusCode) {
                throw "HTTP $([int]$response.StatusCode)"
            }
            if ([string]::IsNullOrWhiteSpace($content)) {
                return $null
            }
            return $content | ConvertFrom-Json
        } finally {
            $response.Dispose()
        }
    } catch {
        throw "Console API request failed: method=$Method path=$Path; $($_.Exception.Message)"
    } finally {
        $request.Dispose()
    }
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
    if ($ForceRelay) {
        $relayBaseline = Invoke-RestMethod -Uri "http://${RelayHost}:$RelayPort/healthz" -TimeoutSec 10 -NoProxy
    }
    $login = Invoke-ConsoleApi -Path '/api/console/sessions' -Method POST -Body @{
        username = [string]$credentials.username
        password = [string]$credentials.password
    }
    $accessToken = [string]$login.token
    if ($accessToken -notmatch '^[0-9a-f]{64}$') {
        throw 'Windows Panel login returned an invalid access token.'
    }

    $applications = @(Invoke-ConsoleApi -Path '/api/console/applications?limit=100' -Token $accessToken)
    $application = if ($AppId) {
        $applications | Where-Object { $_.id -eq $AppId } | Select-Object -First 1
    } else {
        $applications | Where-Object { $_.kind -eq 'webview' } | Select-Object -First 1
    }
    if (-not $application -and -not $AppId) {
        $application = $applications | Where-Object { $_.kind -eq 'game_hook' } | Select-Object -First 1
    }
    if (-not $application) {
        throw 'No matching Windows cloud application is available to the test user.'
    }

    $applicationInstance = Invoke-ConsoleApi -Path '/api/console/instances' -Method POST -Body @{
        request_id = [guid]::NewGuid().ToString()
        application_id = [string]$application.id
        deployment_id = $null
    } -Token $accessToken -UserResource

    $encodedInstanceId = [Uri]::EscapeDataString([string]$applicationInstance.id)
    $startDeadline = [DateTime]::UtcNow.AddSeconds($StartTimeoutSeconds)
    while ($applicationInstance.state -notin @('running', 'failed', 'stopped') -and [DateTime]::UtcNow -lt $startDeadline) {
        Start-Sleep -Milliseconds 500
        $applicationInstance = Invoke-ConsoleApi -Path "/api/console/instances/$encodedInstanceId" -Token $accessToken -UserResource
    }
    if ($applicationInstance.state -ne 'running') {
        throw "Cloud application did not enter running state: $($applicationInstance.state)"
    }

    $resourceSession = Invoke-ConsoleApi -Path '/api/console/resource-sessions' -Method POST -Body @{
        request_id = [guid]::NewGuid().ToString()
        target = @{
            kind = 'cloud_application'
            application_id = [string]$application.id
            instance_id = [string]$applicationInstance.id
        }
        access = 'controller'
    } -Token $accessToken -UserResource

    $encodedSessionId = [Uri]::EscapeDataString([string]$resourceSession.id)
    $requestedSessionId = [string]$resourceSession.id
    $descriptorResponse = Invoke-ConsoleApi -Path "/api/console/resource-sessions/$encodedSessionId/descriptor" -Method POST -Body @{
        revision = [long]$resourceSession.revision
    } -Token $accessToken -UserResource
    $descriptor = $descriptorResponse.descriptor
    $frontendToken = [string]$descriptorResponse.token
    $resourceSession = $descriptor.session
    if (-not $descriptor.host -or [int]$descriptor.port -lt 4613 -or [int]$descriptor.port -gt 4998 -or
        $descriptor.transport -ne 'native' -or $descriptor.session.target.kind -ne 'cloud_application' -or
        $descriptor.session.target.application_id -ne $application.id -or
        $descriptor.session.target.instance_id -ne $applicationInstance.id -or
        $descriptor.session.id -ne $requestedSessionId -or [long]$descriptor.session.revision -le 0 -or
        $frontendToken -notmatch '^[0-9a-f]{64}$') {
        throw 'Console returned an invalid PostgreSQL Native connection descriptor.'
    }
    if (-not (Test-NetConnection -ComputerName ([string]$descriptor.host) -Port ([int]$descriptor.port) -InformationLevel Quiet)) {
        throw 'The dynamic Render TCP endpoint is unreachable.'
    }

    $connectionNonce = [guid]::NewGuid().ToString('N')
    $launch = @{
        schema = 1
        host = [string]$descriptor.host
        port = [int]$descriptor.port
        stream_id = [string]$descriptor.session.id
        connection_instance_id = [string]$applicationInstance.id
        connection_nonce = $connectionNonce
        device_id = "windows_acceptance_$connectionNonce"
        remote_device_id = [string]$applicationInstance.id
        frontend_session_id = [string]$descriptor.session.id
        frontend_session_revision = [long]$descriptor.session.revision
        frontend_token = $frontendToken
        mode = 'desktop'
        stream_name = 'Windows public cloud application acceptance'
        language = 'zh-CN'
        decoder = 'Auto'
        appkey = $relayAppKey
        audio = $true
        clipboard = $true
        only_viewing = $false
        force_tcp = $false
        force_relay = [bool]$ForceRelay
        split_windows = $false
        relay_host = $RelayHost
        relay_port = $RelayPort
        relay_remote_device_id = if ($ForceRelay) { "server_$($applicationInstance.id)" } else { '' }
    }
    $startInfo = [Diagnostics.ProcessStartInfo]::new($clientPath, '--native-launch-stdin')
    $startInfo.WorkingDirectory = Split-Path $clientPath -Parent
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $clientProcess = [Diagnostics.Process]::Start($startInfo)
    $stdoutDrain = $clientProcess.StandardOutput.ReadToEndAsync()
    $stderrDrain = $clientProcess.StandardError.ReadToEndAsync()
    $clientProcess.StandardInput.Write(($launch | ConvertTo-Json -Compress -Depth 12))
    $clientProcess.StandardInput.Close()
    $launch.frontend_token = ''
    $frontendToken = ''
    $launch = $null

    $clientDeadline = [DateTime]::UtcNow.AddSeconds($ClientTimeoutSeconds)
    $frameReady = $false
    $fileTransportReady = $false
    do {
        Start-Sleep -Milliseconds 500
        $clientProcess.Refresh()
        if ($clientProcess.HasExited) {
            throw "Windows Client exited before acceptance completed: exit=$($clientProcess.ExitCode)"
        }
        $evidence = Read-NewClientLog
        $frameReady = $evidence -match 'Video frame came|Video frame stream reset|key frame'
        $fileTransportReady = $evidence -match 'File transfer connected|file transport|ft_data_channel|FT channel|file channel'
    } while ((-not $frameReady -or $clientProcess.MainWindowHandle -eq [IntPtr]::Zero) -and [DateTime]::UtcNow -lt $clientDeadline)

    if (-not $frameReady) {
        throw 'Windows Client did not decode a video frame before the deadline.'
    }
    if ($clientProcess.MainWindowHandle -eq [IntPtr]::Zero) {
        throw 'Windows Client did not expose a workspace window before the deadline.'
    }
    $qtModules = @($clientProcess.Modules | Where-Object { $_.ModuleName -match '^Qt\d' })
    if ($qtModules.Count -ne 0) {
        throw "Windows Client loaded $($qtModules.Count) Qt runtime modules."
    }

    if ($ExerciseInput) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PixelsCloudInputProbe {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
}
'@
        $window = [PixelsCloudInputProbe+Rect]::new()
        if (-not [PixelsCloudInputProbe]::GetWindowRect($clientProcess.MainWindowHandle, [ref]$window)) {
            throw 'Windows Client workspace bounds could not be read for input acceptance.'
        }
        [void][PixelsCloudInputProbe]::SetForegroundWindow($clientProcess.MainWindowHandle)
        $inputX = [int](($window.Left + $window.Right) / 2)
        $inputY = [int](($window.Top + $window.Bottom) / 2)
        [void][PixelsCloudInputProbe]::SetCursorPos($inputX, $inputY)
        [PixelsCloudInputProbe]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [PixelsCloudInputProbe]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    }

    $relayEvidence = $null
    if ($ForceRelay) {
        $relayDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 250
            $relayEvidence = Invoke-RestMethod -Uri "http://${RelayHost}:$RelayPort/healthz" -TimeoutSec 5 -NoProxy
        } while (($relayEvidence.rooms -lt 1 -or
                $relayEvidence.remote_to_creator_payload_bytes -le $relayBaseline.remote_to_creator_payload_bytes -or
                ($ExerciseInput -and $relayEvidence.creator_to_remote_payload_bytes -le $relayBaseline.creator_to_remote_payload_bytes)) -and
            [DateTime]::UtcNow -lt $relayDeadline)
        if ($relayEvidence.rooms -lt 1 -or
            $relayEvidence.remote_to_creator_payload_bytes -le $relayBaseline.remote_to_creator_payload_bytes -or
            ($ExerciseInput -and $relayEvidence.creator_to_remote_payload_bytes -le $relayBaseline.creator_to_remote_payload_bytes)) {
            throw 'Relay did not prove the required active room and bidirectional payload flow.'
        }
    }

    [pscustomobject]@{
        Result = 'PASS'
        Mode = if ($ForceRelay) { 'Native Relay' } else { 'Native Direct' }
        Login = $true
        AppId = [string]$application.id
        AppType = [string]$application.kind
        InstanceState = [string]$applicationInstance.state
        ResourceSessionId = [string]$resourceSession.id
        Endpoint = "$($descriptor.host):$($descriptor.port)"
        DynamicPortValid = $true
        TcpReachable = $true
        WorkspaceReady = $true
        DecodedFrame = $frameReady
        FileTransportEvidence = $fileTransportReady
        InputExercised = [bool]$ExerciseInput
        RelayRoomReady = if ($relayEvidence) { $relayEvidence.rooms -ge 1 } else { $null }
        RelayCreatorToRemoteBytes = if ($relayEvidence) { [long]$relayEvidence.creator_to_remote_payload_bytes } else { $null }
        RelayRemoteToCreatorBytes = if ($relayEvidence) { [long]$relayEvidence.remote_to_creator_payload_bytes } else { $null }
        QtModuleCount = $qtModules.Count
        ClientHash = (Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash
    }
} finally {
    if ($clientProcess -and -not $clientProcess.HasExited) {
        $clientProcess.Kill()
        [void]$clientProcess.WaitForExit(5000)
    }
    if ($resourceSession -and $accessToken) {
        try {
            $encodedSessionId = [Uri]::EscapeDataString([string]$resourceSession.id)
            $currentSession = Invoke-ConsoleApi -Path "/api/console/resource-sessions/$encodedSessionId" -Token $accessToken -UserResource
            [void](Invoke-ConsoleApi -Path "/api/console/resource-sessions/$encodedSessionId/close" -Method POST -Body @{
                revision = [long]$currentSession.revision
            } -Token $accessToken -UserResource)
        } catch {
            Write-Warning "Resource session cleanup failed: $($_.Exception.Message)"
        }
    }
    if ($applicationInstance -and $accessToken) {
        try {
            $encodedInstanceId = [Uri]::EscapeDataString([string]$applicationInstance.id)
            $currentInstance = Invoke-ConsoleApi -Path "/api/console/instances/$encodedInstanceId" -Token $accessToken -UserResource
            if ($currentInstance.state -notin @('stopped', 'failed')) {
                [void](Invoke-ConsoleApi -Path "/api/console/instances/$encodedInstanceId/stop" -Method POST -Body @{
                    revision = [long]$currentInstance.revision
                } -Token $accessToken -UserResource)
            }
        } catch {
            Write-Warning "Cloud application cleanup failed: $($_.Exception.Message)"
        }
    }
    if ($accessToken) {
        try {
            [void](Invoke-ConsoleApi -Path '/api/console/session' -Method DELETE -Token $accessToken)
        } catch {
            Write-Warning "Windows test login cleanup failed: $($_.Exception.Message)"
        }
    }
    $accessToken = ''
    $relayAppKey = ''
    $httpClient.Dispose()
    $httpHandler.Dispose()
    $trustedRoot.Dispose()
}
