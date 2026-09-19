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
    [string]$AppId = '',
    [switch]$Rdp,
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
$clientLogPath = Join-Path (Split-Path $clientPath -Parent) 'px_logs/px_client.log'

if ($Rdp) {
    throw 'The PostgreSQL RDP descriptor does not yet carry the protected workspace bootstrap. RDP cannot be accepted with a fabricated or legacy password.'
}
foreach ($path in @($clientPath, $buildClientPath, $credentialsPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Windows public acceptance input is missing: $path"
    }
}
if ((Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $buildClientPath -Algorithm SHA256).Hash) {
    throw 'The Client product build and dist artifact hashes differ.'
}

$credentials = Get-Content -LiteralPath $credentialsPath -Raw | ConvertFrom-Json
$accessToken = ''
$applicationInstance = $null
$resourceSession = $null
$clientProcess = $null
$clientLogOffset = if (Test-Path -LiteralPath $clientLogPath) { (Get-Item -LiteralPath $clientLogPath).Length } else { 0L }

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

    $headers = @{
        Accept = 'application/json'
        Origin = ([Uri]$ConsoleBase).GetLeftPart([UriPartial]::Authority)
        'X-Pixels-Client-Type' = 'panel'
    }
    if ($Token) {
        $headers.Authorization = "Bearer $Token"
    }
    if ($UserResource) {
        $headers['X-Pixels-Subject-Kind'] = 'user'
    }
    $parameters = @{
        Uri = "$ConsoleBase$Path"
        Method = $Method
        Headers = $headers
        TimeoutSec = 20
    }
    if ($null -ne $Body) {
        $parameters.ContentType = 'application/json'
        $parameters.Body = $Body | ConvertTo-Json -Compress -Depth 12
    }
    try {
        return Invoke-RestMethod @parameters
    } catch {
        throw "Console API request failed: method=$Method path=$Path; $($_.Exception.Message)"
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
        audio = $true
        clipboard = $true
        only_viewing = $false
        force_tcp = $false
        force_relay = $false
        split_windows = $false
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

    [pscustomobject]@{
        Result = 'PASS'
        Mode = 'Native'
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
}
