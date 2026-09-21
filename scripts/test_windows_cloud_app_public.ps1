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
    [switch]$ExerciseAudio,
    [switch]$ExerciseFileTransfer,
    [switch]$ExerciseFileCancelRetry,
    [switch]$ExerciseFileHostRestart,
    [switch]$ExerciseRevocation,
    [ValidateSet('', 'peer_closed', 'user_stopped', 'transport_lost', 'policy_revoked', 'io_error')]
    [string]$ExpectedRdpChannelReason = '',
    [ValidateRange(0, 90)]
    [int]$ActiveLeaseProbeSeconds = 0,
    [ValidateRange(0, 120)]
    [int]$ConnectedHoldSeconds = 0,
    [ValidateRange(15, 180)]
    [int]$StartTimeoutSeconds = 90,
    [ValidateRange(15, 180)]
    [int]$ClientTimeoutSeconds = 60,
    [ValidateRange(15, 90)]
    [int]$RevocationTimeoutSeconds = 60,
    [string]$PublicComputerName = '39.71.45.66'
)

$ErrorActionPreference = 'Stop'
$ConsoleBase = $ConsoleBase.TrimEnd('/')
$repository = Split-Path $PSScriptRoot -Parent
$clientPath = Join-Path $repository 'build_official/client/dist/px_client.exe'
$buildClientPath = Join-Path $repository 'build_official/client/cmake/src/px_deps/px_client.exe'
$credentialsPath = Join-Path $repository '.env/public_test_user.json'
$licensePath = Join-Path $repository '.env/public_license.json'
$machinePath = Join-Path $repository '.env/test_machine.md'
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

foreach ($path in @($clientPath, $buildClientPath, $credentialsPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Windows public acceptance input is missing: $path"
    }
}
if ($ForceRelay -and ((-not $RelayHost) -or $RelayPort -eq 0 -or -not (Test-Path -LiteralPath $licensePath -PathType Leaf))) {
    throw 'Forced Relay acceptance requires an explicit Relay host, port and public license input.'
}
if ($Rdp -and $ForceRelay) {
    throw 'RDP uses its dedicated reliable carrier and cannot be combined with Native Relay.'
}
if ($ExpectedRdpChannelReason -and -not $Rdp) {
    throw 'ExpectedRdpChannelReason requires RDP acceptance.'
}
if ($ExpectedRdpChannelReason -eq 'policy_revoked' -and -not $ExerciseRevocation) {
    throw 'The policy_revoked terminal outcome requires ExerciseRevocation.'
}
if ($ExerciseRevocation -and $ExpectedRdpChannelReason -and $ExpectedRdpChannelReason -ne 'policy_revoked') {
    throw 'ExerciseRevocation cannot be combined with another expected RDP terminal outcome.'
}
if ($ExpectedRdpChannelReason -eq 'io_error' -and $ConnectedHoldSeconds -gt 0) {
    throw 'The io_error probe injects immediately after the first decoded frame and cannot use ConnectedHoldSeconds.'
}
if ($Rdp -and ($ExerciseAudio -or $ExerciseFileTransfer -or $ExerciseFileCancelRetry -or $ExerciseFileHostRestart)) {
    throw 'RDP audio and file capabilities require their dedicated functional acceptance batches.'
}
if (@($ExerciseAudio, $ExerciseFileTransfer, $ExerciseFileCancelRetry, $ExerciseFileHostRestart, $ExerciseRevocation).Where({ $_ }).Count -gt 1) {
    throw 'Audio, file-transfer and online-revocation acceptance use separate client lifecycles and cannot run in the same invocation.'
}
if ($ActiveLeaseProbeSeconds -gt 0 -and -not $ExerciseRevocation) {
    throw 'ActiveLeaseProbeSeconds requires ExerciseRevocation.'
}
$exerciseAnyFileTransfer = $ExerciseFileTransfer -or $ExerciseFileCancelRetry -or $ExerciseFileHostRestart
if ($ExerciseAudio -or $ExerciseFileHostRestart) {
    if (-not (Test-Path -LiteralPath $machinePath -PathType Leaf)) {
        throw "Windows public host acceptance input is missing: $machinePath"
    }
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
$acceptanceRoot = $null
$acceptanceSource = $null
$acceptanceDownloadDirectory = $null
$acceptanceDownloadedFile = $null
$acceptanceSourceHash = $null
$audioRemoteSession = $null
$fileRestartRemoteSession = $null
$audioAdminToken = ''
$revocationAdminToken = ''
$revocationNodeGenerationBefore = $null
$revocationNodeGenerationAfter = $null
$activeLeaseProbePassed = $false
$audioApplicationId = $null
$audioOriginalApplicationSpec = $null
$audioAcceptancePagePath = $null
$previousTrustedHosts = $null
$rdpTerminalChannel = $null

function Invoke-ConsoleApi {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [ValidateSet('GET', 'POST', 'PATCH', 'DELETE')]
        [string]$Method = 'GET',
        [object]$Body = $null,
        [string]$Token = '',
        [ValidateSet('panel', 'admin_web')]
        [string]$ClientType = 'panel',
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
        [void]$request.Headers.TryAddWithoutValidation('X-Pixels-Client-Type', $ClientType)
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

function Wait-RdpTerminalChannel {
    param(
        [Parameter(Mandatory)]
        [string]$SessionId,
        [Parameter(Mandatory)]
        [string]$ExpectedReason
    )

    $encodedChannelSessionId = [Uri]::EscapeDataString($SessionId)
    $channelDeadline = [DateTime]::UtcNow.AddSeconds(15)
    $terminalChannel = $null
    do {
        Start-Sleep -Milliseconds 250
        $sessionChannels = @(Invoke-ConsoleApi -Path "/api/console/activity/channels?session=$encodedChannelSessionId&limit=100" `
                -Token $accessToken -UserResource)
        $terminalChannel = $sessionChannels |
            Where-Object { $_.kind -eq 'rdp' -and $_.state -ne 'active' } |
            Select-Object -First 1
    } while (-not $terminalChannel -and [DateTime]::UtcNow -lt $channelDeadline)
    if (-not $terminalChannel) {
        throw 'RDP resource channel did not publish a terminal activity record.'
    }
    if ([string]$terminalChannel.reason -ne $ExpectedReason) {
        throw "RDP resource channel reason mismatch: expected=$ExpectedReason actual=$($terminalChannel.reason)"
    }
    if ([long]$terminalChannel.sent_bytes -le 0 -or [long]$terminalChannel.received_bytes -le 0 -or
        [long]$terminalChannel.sequence -le 0) {
        throw 'RDP terminal activity record did not retain bidirectional traffic and sequence evidence.'
    }
    return $terminalChannel
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
    } elseif ($Rdp) {
        $applications | Where-Object { $_.kind -eq 'rdp' } | Select-Object -First 1
    } else {
        $applications | Where-Object { $_.kind -eq 'webview' } | Select-Object -First 1
    }
    if (-not $application -and -not $AppId -and -not $Rdp) {
        $application = $applications | Where-Object { $_.kind -eq 'game_hook' } | Select-Object -First 1
    }
    if (-not $application) {
        throw 'No matching Windows cloud application is available to the test user.'
    }

    if ($ExerciseFileHostRestart) {
        $machineText = Get-Content -LiteralPath $machinePath -Raw -Encoding UTF8
        $machinePassword = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
        $machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
        if (-not $machinePassword -or -not $machineName) {
            throw 'Public test host machine-qualified credential is incomplete.'
        }
        $machineCredential = [pscredential]::new(
            "$machineName\Administrator",
            (ConvertTo-SecureString $machinePassword -AsPlainText -Force))
        $machinePassword = ''
        $trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
        $previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
        Set-Item -LiteralPath $trustedHostsPath -Value $PublicComputerName -Force
        $fileRestartRemoteSession = New-PSSession -ComputerName $PublicComputerName -Credential $machineCredential
    }

    if ($ExerciseAudio) {
        if ($application.kind -ne 'webview') {
            throw 'Audio acceptance requires a WebView application so the controlled audio source remains inside the Render session.'
        }
        $machineText = Get-Content -LiteralPath $machinePath -Raw -Encoding UTF8
        $machinePassword = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
        $machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
        if (-not $machinePassword -or -not $machineName) {
            throw 'Public test host machine-qualified credential is incomplete.'
        }
        $machineCredential = [pscredential]::new(
            "$machineName\Administrator",
            (ConvertTo-SecureString $machinePassword -AsPlainText -Force))
        $machinePassword = ''
        $trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
        $previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
        Set-Item -LiteralPath $trustedHostsPath -Value $PublicComputerName -Force
        $audioRemoteSession = New-PSSession -ComputerName $PublicComputerName -Credential $machineCredential

        $audioPageName = 'pixels-audio-acceptance-' + [guid]::NewGuid().ToString('N') + '.html'
        $audioAcceptancePagePath = "D:\PixelsServer\app\console-static\$audioPageName"
        $audioPage = @'
<!doctype html><meta charset="utf-8"><title>Pixels Audio Acceptance</title>
<style>html,body{margin:0;width:100%;height:100%;background:#101828;color:#fff;font:32px sans-serif;display:grid;place-items:center}</style>
<canvas id="view" width="1280" height="720"></canvas>
<script>
const canvas=document.getElementById('view');const drawing=canvas.getContext('2d');let frame=0;
function paint(){frame++;drawing.fillStyle=`hsl(${frame%360} 70% 25%)`;drawing.fillRect(0,0,1280,720);drawing.fillStyle='#fff';drawing.fillText('Pixels audio acceptance',390,360);requestAnimationFrame(paint)}paint();
const audioContext=new AudioContext();const oscillator=audioContext.createOscillator();const gain=audioContext.createGain();
oscillator.frequency.value=440;gain.gain.value=0.2;oscillator.connect(gain).connect(audioContext.destination);oscillator.start();
audioContext.resume();setInterval(()=>audioContext.resume(),500);
</script>
'@
        Invoke-Command -Session $audioRemoteSession -ArgumentList $audioAcceptancePagePath, $audioPage -ScriptBlock {
            param($pagePath, $pageContent)
            $staticDirectory = [IO.Path]::GetFullPath('D:\PixelsServer\app\console-static')
            $resolvedPagePath = [IO.Path]::GetFullPath($pagePath)
            if (-not [IO.Path]::GetDirectoryName($resolvedPagePath).Equals($staticDirectory, [StringComparison]::OrdinalIgnoreCase) -or
                -not [IO.Path]::GetFileName($resolvedPagePath).StartsWith('pixels-audio-acceptance-', [StringComparison]::Ordinal) -or
                [IO.Path]::GetExtension($resolvedPagePath) -ne '.html') {
                throw 'Remote audio acceptance page failed its safety boundary check.'
            }
            $retiredToneDirectory = [IO.Path]::GetFullPath('C:\ProgramData\Pixels\acceptance')
            Get-CimInstance Win32_Process |
                Where-Object {
                    $_.ExecutablePath -and [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($_.ExecutablePath)).Equals(
                        $retiredToneDirectory, [StringComparison]::OrdinalIgnoreCase) -and
                    [IO.Path]::GetFileName($_.ExecutablePath).StartsWith('Pixels-Audio-Acceptance-', [StringComparison]::Ordinal)
                } |
                ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
            Get-ScheduledTask -TaskName 'Pixels-Audio-Acceptance-*' -ErrorAction SilentlyContinue |
                ForEach-Object { Unregister-ScheduledTask -TaskName $_.TaskName -Confirm:$false }
            Get-ChildItem -LiteralPath $retiredToneDirectory -Filter 'Pixels-Audio-Acceptance-*.exe' -File -ErrorAction SilentlyContinue |
                Remove-Item -Force
            [IO.File]::WriteAllText($resolvedPagePath, $pageContent, [Text.UTF8Encoding]::new($false))
        }

        $adminLogin = Invoke-ConsoleApi -Path '/api/console/sessions' -Method POST -Body @{
            username = [string]$credentials.username
            password = [string]$credentials.password
        } -ClientType admin_web
        $audioAdminToken = [string]$adminLogin.token
        if ($audioAdminToken -notmatch '^[0-9a-f]{64}$') {
            throw 'Windows audio acceptance administrator login returned an invalid access token.'
        }
        $managedApplications = @(Invoke-ConsoleApi -Path '/api/console/managed/applications?limit=100' -Token $audioAdminToken -ClientType admin_web)
        $managedApplication = $managedApplications | Where-Object { $_.id -eq $application.id } | Select-Object -First 1
        if (-not $managedApplication) {
            throw 'The selected WebView application is not available through the managed catalog.'
        }
        $audioApplicationId = [string]$managedApplication.id
        $audioOriginalApplicationSpec = $managedApplication.spec
        $audioAcceptanceSpec = @{
            name = [string]$managedApplication.spec.name
            launch = @{
                kind = 'webview'
                entry_url = "$ConsoleBase/$audioPageName"
                video = $managedApplication.spec.launch.video
            }
            access = [string]$managedApplication.spec.access
            allow_observer = [bool]$managedApplication.spec.allow_observer
            allow_takeover = [bool]$managedApplication.spec.allow_takeover
            disabled = [bool]$managedApplication.spec.disabled
        }
        $updatedApplication = Invoke-ConsoleApi -Path "/api/console/managed/applications/$audioApplicationId" -Method PATCH -Body @{
            revision = [long]$managedApplication.revision
            spec = $audioAcceptanceSpec
        } -Token $audioAdminToken -ClientType admin_web
        $managedDeployments = @(Invoke-ConsoleApi -Path '/api/console/managed/deployments?limit=100' -Token $audioAdminToken -ClientType admin_web)
        $audioDeployments = @($managedDeployments | Where-Object { $_.application_id -eq $audioApplicationId })
        if ($audioDeployments.Count -eq 0) {
            throw 'The selected WebView application has no managed deployment.'
        }
        foreach ($deployment in $audioDeployments) {
            $deploymentConfiguration = @{
                target = @{ kind = 'webview' }
                gpu_key = $deployment.gpu_key
                gpu_profile = @{
                    memory_bytes = [long]$deployment.gpu_memory_bytes
                    compute_per_mille = [int]$deployment.gpu_compute_per_mille
                    encoder_per_mille = [int]$deployment.gpu_encoder_per_mille
                    memory_reserve_bytes = [long]$deployment.gpu_memory_reserve_bytes
                    compute_limit_per_mille = [int]$deployment.gpu_compute_limit_per_mille
                    encoder_limit_per_mille = [int]$deployment.gpu_encoder_limit_per_mille
                }
                capacity = [int]$deployment.capacity
                disabled = [bool]$deployment.disabled
            }
            [void](Invoke-ConsoleApi -Path "/api/console/managed/deployments/$($deployment.id)" -Method PATCH -Body @{
                revision = [long]$deployment.revision
                configuration = $deploymentConfiguration
            } -Token $audioAdminToken -ClientType admin_web)
        }
        $deploymentDeadline = [DateTime]::UtcNow.AddSeconds(45)
        do {
            Start-Sleep -Milliseconds 500
            $managedDeployments = @(Invoke-ConsoleApi -Path '/api/console/managed/deployments?limit=100' -Token $audioAdminToken -ClientType admin_web)
            $audioDeployments = @($managedDeployments | Where-Object { $_.application_id -eq $audioApplicationId })
            $audioDeploymentsReady = $audioDeployments.Count -gt 0 -and @($audioDeployments | Where-Object {
                    $_.observed_state -eq 'ready' -and [long]$_.application_revision -eq [long]$updatedApplication.revision
                }).Count -eq $audioDeployments.Count
        } while (-not $audioDeploymentsReady -and [DateTime]::UtcNow -lt $deploymentDeadline)
        if (-not $audioDeploymentsReady) {
            throw 'The controlled-audio WebView deployment did not become ready.'
        }
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
    $rdpBootstrap = $descriptorResponse.rdp
    $frontendToken = [string]$descriptorResponse.token
    $resourceSession = $descriptor.session
    $expectedTransport = if ($Rdp) { 'rdp' } else { 'native' }
    if (-not $descriptor.host -or [int]$descriptor.port -lt 4613 -or [int]$descriptor.port -gt 4998 -or
        $descriptor.transport -ne $expectedTransport -or $descriptor.session.target.kind -ne 'cloud_application' -or
        $descriptor.session.target.application_id -ne $application.id -or
        $descriptor.session.target.instance_id -ne $applicationInstance.id -or
        $descriptor.session.id -ne $requestedSessionId -or [long]$descriptor.session.revision -le 0 -or
        $frontendToken -notmatch '^[0-9a-f]{64}$') {
        throw "Console returned an invalid PostgreSQL $expectedTransport connection descriptor."
    }
    if ($Rdp -and (-not $rdpBootstrap -or
            [string]$rdpBootstrap.account_name -notmatch '^pxrdp_[0-9a-f]{14}$' -or
            [string]$rdpBootstrap.domain -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]{0,254}$' -or
            [string]$rdpBootstrap.proxy_certificate_sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            [string]::IsNullOrWhiteSpace([string]$rdpBootstrap.password))) {
        throw 'Console returned an invalid protected RDP workspace bootstrap.'
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
    if ($Rdp) {
        $launch.rdp = $rdpBootstrap
    }
    if ($ExpectedRdpChannelReason -eq 'io_error') {
        $launch.acceptance_rdp_io_error = $true
    }
    if ($ExpectedRdpChannelReason -eq 'peer_closed') {
        $launch.acceptance_rdp_peer_close = $true
    }
    if ($exerciseAnyFileTransfer) {
        $acceptanceRoot = Join-Path ([IO.Path]::GetTempPath()) ("pixels-file-transfer-" + [guid]::NewGuid().ToString('N'))
        $acceptanceSourceDirectory = Join-Path $acceptanceRoot 'source'
        $acceptanceDownloadDirectory = Join-Path $acceptanceRoot 'download'
        [void][IO.Directory]::CreateDirectory($acceptanceSourceDirectory)
        [void][IO.Directory]::CreateDirectory($acceptanceDownloadDirectory)
        $acceptanceFileName = "pixels-acceptance-$([guid]::NewGuid().ToString('N')).bin"
        $acceptanceSource = Join-Path $acceptanceSourceDirectory $acceptanceFileName
        $acceptanceDownloadedFile = Join-Path $acceptanceDownloadDirectory $acceptanceFileName
        $acceptancePayload = [byte[]]::new($(if ($ExerciseFileHostRestart) { 4MB } elseif ($ExerciseFileCancelRetry) { 4MB } else { 1MB }))
        $acceptanceRandom = [Security.Cryptography.RandomNumberGenerator]::Create()
        try {
            $acceptanceRandom.GetBytes($acceptancePayload)
        } finally {
            $acceptanceRandom.Dispose()
        }
        [IO.File]::WriteAllBytes($acceptanceSource, $acceptancePayload)
        [Array]::Clear($acceptancePayload, 0, $acceptancePayload.Length)
        $acceptanceSourceHash = (Get-FileHash -LiteralPath $acceptanceSource -Algorithm SHA256).Hash
        $launch.acceptance_file_transfer = @{
            local_source_path = $acceptanceSource
            remote_directory = 'C:\Windows\Temp'
            local_download_directory = $acceptanceDownloadDirectory
            exercise_cancel_retry = [bool]$ExerciseFileCancelRetry
            exercise_host_restart = [bool]$ExerciseFileHostRestart
        }
    }
    if ($ExerciseAudio) {
        $launch.acceptance_audio = $true
    }
    $clientArguments = if ($ExpectedRdpChannelReason -eq 'io_error') {
        '--rdp-launch-stdin --acceptance-rdp-io-error'
    } elseif ($ExpectedRdpChannelReason -eq 'peer_closed') {
        '--rdp-launch-stdin --acceptance-rdp-peer-close'
    } elseif ($Rdp) {
        '--rdp-launch-stdin'
    } elseif ($exerciseAnyFileTransfer) {
        '--native-launch-stdin --acceptance-file-transfer'
    } elseif ($ExerciseAudio) {
        '--native-launch-stdin --acceptance-audio'
    } else {
        '--native-launch-stdin'
    }
    $startInfo = [Diagnostics.ProcessStartInfo]::new($clientPath, $clientArguments)
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
    $workspaceReady = $false
    $qtModuleCount = $null
    $fileTransportReady = $false
    do {
        Start-Sleep -Milliseconds 500
        $clientProcess.Refresh()
        $workspaceReady = $workspaceReady -or $clientProcess.MainWindowHandle -ne [IntPtr]::Zero
        if ($null -eq $qtModuleCount -and -not $clientProcess.HasExited) {
            $qtModuleCount = @($clientProcess.Modules | Where-Object { $_.ModuleName -match '^Qt\d' }).Count
        }
        $evidence = Read-NewClientLog
        $frameReady = $frameReady -or $(if ($Rdp) {
                $evidence -match 'event=rdp\.frame\.progress published=[1-9][0-9]*' -or
                ($ExpectedRdpChannelReason -eq 'io_error' -and
                    $evidence -match 'event=rdp\.acceptance operation=inject_invalid_packet outcome=sent') -or
                ($ExpectedRdpChannelReason -eq 'peer_closed' -and
                    $evidence -match 'event=rdp\.acceptance operation=close_peer outcome=sent')
            } else {
                $evidence -match 'Video frame came|Video frame stream reset|key frame'
            })
        $fileTransportReady = $fileTransportReady -or $evidence -match 'File transfer connected|file transport|ft_data_channel|FT channel|file channel'
        if ($clientProcess.HasExited) {
            if ($ExerciseAudio -or $exerciseAnyFileTransfer) {
                break
            }
            throw "Windows Client exited before acceptance completed: exit=$($clientProcess.ExitCode)"
        }
    } while ((-not $frameReady -or
            (-not ($ExerciseAudio -or $exerciseAnyFileTransfer) -and $clientProcess.MainWindowHandle -eq [IntPtr]::Zero)) -and
        [DateTime]::UtcNow -lt $clientDeadline)

    if (-not $frameReady) {
        throw 'Windows Client did not decode a video frame before the deadline.'
    }
    if ($ExerciseAudio -or $exerciseAnyFileTransfer) {
        $workspaceReady = $frameReady
    }
    if (-not $workspaceReady) {
        throw 'Windows Client did not expose a workspace window before the deadline.'
    }
    if ($null -eq $qtModuleCount) {
        throw 'Windows Client module inventory was not captured before exit.'
    }
    if ($qtModuleCount -ne 0) {
        throw "Windows Client loaded $qtModuleCount Qt runtime modules."
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

    if ($ConnectedHoldSeconds -gt 0) {
        $holdDeadline = [DateTime]::UtcNow.AddSeconds($ConnectedHoldSeconds)
        do {
            Start-Sleep -Milliseconds 500
            if ($clientProcess.HasExited) {
                throw "Windows Client exited during the $ConnectedHoldSeconds-second connected hold."
            }
            $heldSession = Invoke-ConsoleApi -Path "/api/console/resource-sessions/$encodedSessionId" -Token $accessToken -UserResource
            if ($heldSession.state -ne 'connected') {
                throw "The resource session left connected state during the connected hold: $($heldSession.state)"
            }
        } while ([DateTime]::UtcNow -lt $holdDeadline)
    }

    if ($ExpectedRdpChannelReason -in @('peer_closed', 'transport_lost', 'user_stopped', 'io_error')) {
        switch ($ExpectedRdpChannelReason) {
            'peer_closed' {
                $peerCloseDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 250
                    $peerCloseEvidence = Read-NewClientLog
                } while ($peerCloseEvidence -notmatch 'event=rdp\.acceptance operation=close_peer outcome=sent' -and
                    [DateTime]::UtcNow -lt $peerCloseDeadline)
                if ($peerCloseEvidence -notmatch 'event=rdp\.acceptance operation=close_peer outcome=sent') {
                    throw 'Windows Client did not issue the graceful RDP peer close.'
                }
            }
            'transport_lost' {
                $clientProcess.Kill()
                if (-not $clientProcess.WaitForExit(5000)) {
                    throw 'Windows Client process did not terminate for the transport-loss probe.'
                }
            }
            'user_stopped' {
                $currentInstance = Invoke-ConsoleApi -Path "/api/console/instances/$encodedInstanceId" -Token $accessToken -UserResource
                [void](Invoke-ConsoleApi -Path "/api/console/instances/$encodedInstanceId/stop" -Method POST -Body @{
                        revision = [long]$currentInstance.revision
                    } -Token $accessToken -UserResource)
            }
            'io_error' {
                $ioErrorDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 250
                    $ioErrorEvidence = Read-NewClientLog
                } while ($ioErrorEvidence -notmatch 'event=rdp\.acceptance operation=inject_invalid_packet outcome=sent' -and
                    [DateTime]::UtcNow -lt $ioErrorDeadline)
                if ($ioErrorEvidence -notmatch 'event=rdp\.acceptance operation=inject_invalid_packet outcome=sent') {
                    throw 'Windows Client did not inject the authenticated invalid RDP packet.'
                }
            }
        }
        $rdpTerminalChannel = Wait-RdpTerminalChannel -SessionId ([string]$resourceSession.id) -ExpectedReason $ExpectedRdpChannelReason
    }

    $relayEvidence = $null
    $revocationElapsedMilliseconds = $null
    $revocationSessionState = $null
    $revocationRelayRooms = $null
    $revocationClientDisconnected = $false
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
            throw "Relay did not prove the required active room and payload flow: rooms=$($relayEvidence.rooms), " +
                "remote_to_creator=$($relayEvidence.remote_to_creator_payload_bytes)/$($relayBaseline.remote_to_creator_payload_bytes), " +
                "creator_to_remote=$($relayEvidence.creator_to_remote_payload_bytes)/$($relayBaseline.creator_to_remote_payload_bytes)"
        }
    }

    $fileRestartState = $null
    $hostRestartProcessRetired = $null
    if ($ExerciseFileHostRestart) {
        $activeTransferDeadline = [DateTime]::UtcNow.AddSeconds(35)
        do {
            Start-Sleep -Milliseconds 250
            $transferRecords = @(Invoke-ConsoleApi -Path '/api/console/file-transfers?limit=100' -Token $accessToken -UserResource)
            $activeTransfer = $transferRecords |
                Where-Object { $_.session_id -eq [string]$resourceSession.id -and $_.state -eq 'active' } |
                Select-Object -First 1
        } while (-not $activeTransfer -and [DateTime]::UtcNow -lt $activeTransferDeadline)
        if (-not $activeTransfer) {
            throw 'Cloud Node did not report an active file transfer before the restart test.'
        }
        Invoke-Command -Session $fileRestartRemoteSession -ScriptBlock {
            Restart-Service -Name 'px_service' -Force
            $service = Get-Service -Name 'px_service'
            $service.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Running, [TimeSpan]::FromSeconds(30))
        }
        $reconciliationDeadline = [DateTime]::UtcNow.AddSeconds(45)
        do {
            Start-Sleep -Milliseconds 500
            $transferRecords = @(Invoke-ConsoleApi -Path '/api/console/file-transfers?limit=100' -Token $accessToken -UserResource)
            $restartedTransfer = $transferRecords |
                Where-Object { $_.session_id -eq [string]$resourceSession.id -and $_.state -eq 'unknown' } |
                Select-Object -First 1
            if ($restartedTransfer) {
                $fileRestartState = [string]$restartedTransfer.state
            }
        } while (-not $fileRestartState -and [DateTime]::UtcNow -lt $reconciliationDeadline)
        if ($fileRestartState -ne 'unknown') {
            $observedStates = @($transferRecords |
                    Where-Object { $_.session_id -eq [string]$resourceSession.id } |
                    ForEach-Object { $_.state }) -join ','
            throw "Cloud Node Service restart did not reconcile the active file transfer to unknown; observed=$observedStates"
        }
        $processRetirementDeadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $remainingInstanceProcesses = Invoke-Command -Session $fileRestartRemoteSession -ArgumentList ([string]$applicationInstance.id) -ScriptBlock {
                param($instanceId)

                $expectedRenderPath = 'C:\Program Files\Pixels Cloud Node\px_render.exe'
                return @(Get-CimInstance Win32_Process | Where-Object {
                        $_.ExecutablePath -eq $expectedRenderPath -and
                        $_.CommandLine -like "*--webview_instance_id=$instanceId*"
                    }).Count
            }
            if ($remainingInstanceProcesses -gt 0) {
                Start-Sleep -Milliseconds 250
            }
        } while ($remainingInstanceProcesses -gt 0 -and [DateTime]::UtcNow -lt $processRetirementDeadline)
        if ($remainingInstanceProcesses -ne 0) {
            throw "Cloud Node Service restart left $remainingInstanceProcesses tracked application Render process(es) running."
        }
        $hostRestartProcessRetired = $true
        $fileTransportReady = $true
    }

    if ($ExerciseRevocation) {
        $revocationAdminLogin = Invoke-ConsoleApi -Path '/api/console/sessions' -Method POST -Body @{
            username = [string]$credentials.username
            password = [string]$credentials.password
        } -ClientType admin_web
        $revocationAdminToken = [string]$revocationAdminLogin.token
        if ($revocationAdminToken -notmatch '^[0-9a-f]{64}$') {
            throw 'Windows revocation acceptance administrator login returned an invalid access token.'
        }
        $managedNodes = @(Invoke-ConsoleApi -Path '/api/console/managed/nodes?limit=100' -Token $revocationAdminToken -ClientType admin_web)
        $revocationNode = $managedNodes | Where-Object { $_.id -eq [string]$descriptor.node_id } | Select-Object -First 1
        if (-not $revocationNode -or [long]$revocationNode.generation -le 0) {
            throw 'Online revocation could not resolve the descriptor node generation.'
        }
        $revocationNodeGenerationBefore = [long]$revocationNode.generation
        if ($ActiveLeaseProbeSeconds -gt 0) {
            $leaseProbeDeadline = [DateTime]::UtcNow.AddSeconds($ActiveLeaseProbeSeconds)
            do {
                Start-Sleep -Milliseconds 500
                if ($clientProcess.HasExited) {
                    throw "The Client exited during the $ActiveLeaseProbeSeconds-second active lease probe."
                }
                $activeSession = Invoke-ConsoleApi -Path "/api/console/resource-sessions/$encodedSessionId" -Token $accessToken -UserResource
                if ($activeSession.state -ne 'connected') {
                    throw "The resource session left connected state during the active lease probe: $($activeSession.state)"
                }
                $managedNodes = @(Invoke-ConsoleApi -Path '/api/console/managed/nodes?limit=100' -Token $revocationAdminToken -ClientType admin_web)
                $revocationNode = $managedNodes | Where-Object { $_.id -eq [string]$descriptor.node_id } | Select-Object -First 1
                if (-not $revocationNode -or [long]$revocationNode.generation -ne $revocationNodeGenerationBefore) {
                    throw 'The node-control generation changed during the active lease probe.'
                }
            } while ([DateTime]::UtcNow -lt $leaseProbeDeadline)
            $activeLeaseProbePassed = $true
        }
        $revocationStarted = [Diagnostics.Stopwatch]::StartNew()
        $currentSession = Invoke-ConsoleApi -Path "/api/console/resource-sessions/$encodedSessionId" -Token $accessToken -UserResource
        $closingSession = Invoke-ConsoleApi -Path "/api/console/resource-sessions/$encodedSessionId/close" -Method POST -Body @{
            revision = [long]$currentSession.revision
        } -Token $accessToken -UserResource
        if ($closingSession.state -notin @('closing', 'closed')) {
            throw "Online revocation returned an invalid resource-session state: $($closingSession.state)"
        }

        $revocationDeadline = [DateTime]::UtcNow.AddSeconds($RevocationTimeoutSeconds)
        do {
            Start-Sleep -Milliseconds 250
            $closedSession = Invoke-ConsoleApi -Path "/api/console/resource-sessions/$encodedSessionId" -Token $accessToken -UserResource
            $revocationSessionState = [string]$closedSession.state
            $revocationClientDisconnected = $revocationClientDisconnected -or
                (Read-NewClientLog) -match 'SDK websocket disconnected|Relay websocket disconnected'
            if ($ForceRelay) {
                $revocationRelayEvidence = Invoke-RestMethod -Uri "http://${RelayHost}:$RelayPort/healthz" -TimeoutSec 5 -NoProxy
                $revocationRelayRooms = [long]$revocationRelayEvidence.rooms
            }
        } while (($revocationSessionState -ne 'closed' -or -not $revocationClientDisconnected -or
                ($ForceRelay -and $revocationRelayRooms -gt [long]$relayBaseline.rooms)) -and
            [DateTime]::UtcNow -lt $revocationDeadline)
        $revocationStarted.Stop()
        $revocationElapsedMilliseconds = $revocationStarted.ElapsedMilliseconds

        if ($revocationSessionState -ne 'closed') {
            throw "Online revocation did not retire the resource session before the deadline: state=$revocationSessionState"
        }
        if (-not $revocationClientDisconnected) {
            throw 'Online revocation did not disconnect the Client control transport before the deadline.'
        }
        if ($ForceRelay -and $revocationRelayRooms -gt [long]$relayBaseline.rooms) {
            throw "Online revocation did not drain Relay rooms before the deadline: baseline=$($relayBaseline.rooms) current=$revocationRelayRooms"
        }
        $managedNodes = @(Invoke-ConsoleApi -Path '/api/console/managed/nodes?limit=100' -Token $revocationAdminToken -ClientType admin_web)
        $revocationNode = $managedNodes | Where-Object { $_.id -eq [string]$descriptor.node_id } | Select-Object -First 1
        if (-not $revocationNode -or [long]$revocationNode.generation -le 0) {
            throw 'Online revocation could not re-read the descriptor node generation.'
        }
        $revocationNodeGenerationAfter = [long]$revocationNode.generation
        if ($revocationNodeGenerationAfter -ne $revocationNodeGenerationBefore) {
            throw "Online revocation reset the node-control connection: generation=$revocationNodeGenerationBefore->$revocationNodeGenerationAfter"
        }

        if ($ExpectedRdpChannelReason -eq 'policy_revoked') {
            $rdpTerminalChannel = Wait-RdpTerminalChannel -SessionId ([string]$resourceSession.id) -ExpectedReason $ExpectedRdpChannelReason
        }
    }

    if ($exerciseAnyFileTransfer) {
        if ($ExerciseFileHostRestart) {
            if (-not $clientProcess.HasExited) {
                $clientProcess.Kill()
                [void]$clientProcess.WaitForExit(5000)
            }
        } else {
            if (-not $clientProcess.WaitForExit(90000)) {
                throw 'Windows Client did not finish the file-transfer acceptance before the deadline.'
            }
            $acceptanceOutput = $stdoutDrain.GetAwaiter().GetResult()
            [void]$stderrDrain.GetAwaiter().GetResult()
            if ($clientProcess.ExitCode -ne 0 -or $acceptanceOutput -notmatch 'PIXELS_FILE_TRANSFER_ACCEPTANCE=PASS') {
                throw "Windows Client file-transfer acceptance failed: exit=$($clientProcess.ExitCode)"
            }
            if ($ExerciseFileCancelRetry -and $acceptanceOutput -notmatch 'PIXELS_FILE_TRANSFER_CANCEL_RETRY=PASS') {
                throw 'Windows Client did not prove the file-transfer cancel/retry path.'
            }
            if (-not (Test-Path -LiteralPath $acceptanceDownloadedFile -PathType Leaf)) {
                throw 'Windows Client did not create the downloaded acceptance file.'
            }
            $acceptanceDownloadHash = (Get-FileHash -LiteralPath $acceptanceDownloadedFile -Algorithm SHA256).Hash
            if ($acceptanceDownloadHash -ne $acceptanceSourceHash) {
                throw 'Relay file-transfer download hash does not match the uploaded source.'
            }
            $fileTransportReady = $true
        }
    }

    $audioDecoded = $false
    if ($ExerciseAudio) {
        if (-not $clientProcess.HasExited -and -not $clientProcess.WaitForExit(65000)) {
            throw 'Windows Client did not finish the audio acceptance before the deadline.'
        }
        $acceptanceOutput = $stdoutDrain.GetAwaiter().GetResult()
        [void]$stderrDrain.GetAwaiter().GetResult()
        if ($clientProcess.ExitCode -ne 0 -or $acceptanceOutput -notmatch 'PIXELS_AUDIO_ACCEPTANCE=PASS') {
            $audioDiagnostics = @([regex]::Matches($acceptanceOutput, '(?m)^(?:Decode error: .+|PIXELS_AUDIO_ACCEPTANCE=.+)$') |
                    Select-Object -Last 5 |
                    ForEach-Object Value) -join '; '
            throw "Windows Client audio acceptance failed: exit=$($clientProcess.ExitCode); diagnostics=$audioDiagnostics"
        }
        $audioDecoded = $true
    }

    [pscustomobject]@{
        Result = 'PASS'
        Mode = if ($Rdp) { 'RDP' } elseif ($ForceRelay) { 'Native Relay' } else { 'Native Direct' }
        Login = $true
        AppId = [string]$application.id
        AppType = [string]$application.kind
        InstanceState = [string]$applicationInstance.state
        ResourceSessionId = [string]$resourceSession.id
        Endpoint = "$($descriptor.host):$($descriptor.port)"
        DynamicPortValid = $true
        TcpReachable = $true
        RdpProtectedBootstrap = if ($Rdp) { $true } else { $null }
        WorkspaceReady = $workspaceReady
        DecodedFrame = $frameReady
        AudioExercised = [bool]$ExerciseAudio
        AudioDecoded = $audioDecoded
        FileTransportEvidence = $fileTransportReady
        FileTransferExercised = [bool]$exerciseAnyFileTransfer
        FileTransferCancelRetry = [bool]$ExerciseFileCancelRetry
        FileTransferHostRestart = [bool]$ExerciseFileHostRestart
        FileTransferRestartState = $fileRestartState
        FileTransferHostProcessRetired = $hostRestartProcessRetired
        FileTransferSha256 = if ($exerciseAnyFileTransfer) { $acceptanceSourceHash } else { $null }
        InputExercised = [bool]$ExerciseInput
        RelayRoomReady = if ($relayEvidence) { $relayEvidence.rooms -ge 1 } else { $null }
        RelayCreatorToRemoteBytes = if ($relayEvidence) { [long]$relayEvidence.creator_to_remote_payload_bytes } else { $null }
        RelayRemoteToCreatorBytes = if ($relayEvidence) { [long]$relayEvidence.remote_to_creator_payload_bytes } else { $null }
        RevocationExercised = [bool]$ExerciseRevocation
        RevocationSessionState = $revocationSessionState
        RevocationClientDisconnected = if ($ExerciseRevocation) { $revocationClientDisconnected } else { $null }
        RevocationRelayRooms = $revocationRelayRooms
        RevocationElapsedMilliseconds = $revocationElapsedMilliseconds
        RevocationNodeGenerationBefore = $revocationNodeGenerationBefore
        RevocationNodeGenerationAfter = $revocationNodeGenerationAfter
        RdpTerminalChannelState = if ($rdpTerminalChannel) { [string]$rdpTerminalChannel.state } else { $null }
        RdpTerminalChannelReason = if ($rdpTerminalChannel) { [string]$rdpTerminalChannel.reason } else { $null }
        RdpTerminalSentBytes = if ($rdpTerminalChannel) { [long]$rdpTerminalChannel.sent_bytes } else { $null }
        RdpTerminalReceivedBytes = if ($rdpTerminalChannel) { [long]$rdpTerminalChannel.received_bytes } else { $null }
        ActiveLeaseProbeSeconds = $ActiveLeaseProbeSeconds
        ActiveLeaseProbePassed = if ($ActiveLeaseProbeSeconds -gt 0) { $activeLeaseProbePassed } else { $null }
        ConnectedHoldSeconds = $ConnectedHoldSeconds
        QtModuleCount = $qtModuleCount
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
            if ($currentSession.state -notin @('closing', 'closed')) {
                [void](Invoke-ConsoleApi -Path "/api/console/resource-sessions/$encodedSessionId/close" -Method POST -Body @{
                    revision = [long]$currentSession.revision
                } -Token $accessToken -UserResource)
            }
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
            $instanceCleanupDeadline = [DateTime]::UtcNow.AddSeconds(20)
            do {
                $currentInstance = Invoke-ConsoleApi -Path "/api/console/instances/$encodedInstanceId" -Token $accessToken -UserResource
                if ($currentInstance.state -notin @('stopped', 'failed')) {
                    Start-Sleep -Milliseconds 250
                }
            } while ($currentInstance.state -notin @('stopped', 'failed') -and [DateTime]::UtcNow -lt $instanceCleanupDeadline)
            if ($currentInstance.state -notin @('stopped', 'failed')) {
                throw "Cloud application cleanup did not reach a terminal state: $($currentInstance.state)"
            }
        } catch {
            Write-Warning "Cloud application cleanup failed: $($_.Exception.Message)"
        }
    }
    if ($audioApplicationId -and $audioOriginalApplicationSpec -and $audioAdminToken) {
        try {
            $managedApplications = @(Invoke-ConsoleApi -Path '/api/console/managed/applications?limit=100' -Token $audioAdminToken -ClientType admin_web)
            $managedApplication = $managedApplications | Where-Object { $_.id -eq $audioApplicationId } | Select-Object -First 1
            if (-not $managedApplication) {
                throw 'The audio acceptance application disappeared before restoration.'
            }
            $restoredApplication = Invoke-ConsoleApi -Path "/api/console/managed/applications/$audioApplicationId" -Method PATCH -Body @{
                revision = [long]$managedApplication.revision
                spec = $audioOriginalApplicationSpec
            } -Token $audioAdminToken -ClientType admin_web
            $managedDeployments = @(Invoke-ConsoleApi -Path '/api/console/managed/deployments?limit=100' -Token $audioAdminToken -ClientType admin_web)
            $audioDeployments = @($managedDeployments | Where-Object { $_.application_id -eq $audioApplicationId })
            foreach ($deployment in $audioDeployments) {
                $deploymentConfiguration = @{
                    target = @{ kind = 'webview' }
                    gpu_key = $deployment.gpu_key
                    gpu_profile = @{
                        memory_bytes = [long]$deployment.gpu_memory_bytes
                        compute_per_mille = [int]$deployment.gpu_compute_per_mille
                        encoder_per_mille = [int]$deployment.gpu_encoder_per_mille
                        memory_reserve_bytes = [long]$deployment.gpu_memory_reserve_bytes
                        compute_limit_per_mille = [int]$deployment.gpu_compute_limit_per_mille
                        encoder_limit_per_mille = [int]$deployment.gpu_encoder_limit_per_mille
                    }
                    capacity = [int]$deployment.capacity
                    disabled = [bool]$deployment.disabled
                }
                [void](Invoke-ConsoleApi -Path "/api/console/managed/deployments/$($deployment.id)" -Method PATCH -Body @{
                    revision = [long]$deployment.revision
                    configuration = $deploymentConfiguration
                } -Token $audioAdminToken -ClientType admin_web)
            }
            $restoreDeadline = [DateTime]::UtcNow.AddSeconds(45)
            do {
                Start-Sleep -Milliseconds 500
                $managedDeployments = @(Invoke-ConsoleApi -Path '/api/console/managed/deployments?limit=100' -Token $audioAdminToken -ClientType admin_web)
                $audioDeployments = @($managedDeployments | Where-Object { $_.application_id -eq $audioApplicationId })
                $audioDeploymentsReady = $audioDeployments.Count -gt 0 -and @($audioDeployments | Where-Object {
                        $_.observed_state -eq 'ready' -and [long]$_.application_revision -eq [long]$restoredApplication.revision
                    }).Count -eq $audioDeployments.Count
            } while (-not $audioDeploymentsReady -and [DateTime]::UtcNow -lt $restoreDeadline)
            if (-not $audioDeploymentsReady) {
                throw 'The restored WebView deployment did not return to ready state.'
            }
        } catch {
            Write-Warning "Audio acceptance application restoration failed: $($_.Exception.Message)"
        }
    }
    if ($accessToken) {
        try {
            [void](Invoke-ConsoleApi -Path '/api/console/session' -Method DELETE -Token $accessToken)
        } catch {
            Write-Warning "Windows test login cleanup failed: $($_.Exception.Message)"
        }
    }
    if ($audioAdminToken) {
        try {
            [void](Invoke-ConsoleApi -Path '/api/console/session' -Method DELETE -Token $audioAdminToken -ClientType admin_web)
        } catch {
            Write-Warning "Windows audio acceptance administrator logout failed: $($_.Exception.Message)"
        }
    }
    if ($revocationAdminToken) {
        try {
            [void](Invoke-ConsoleApi -Path '/api/console/session' -Method DELETE -Token $revocationAdminToken -ClientType admin_web)
        } catch {
            Write-Warning "Windows revocation acceptance administrator logout failed: $($_.Exception.Message)"
        }
    }
    $accessToken = ''
    $audioAdminToken = ''
    $revocationAdminToken = ''
    $relayAppKey = ''
    if ($audioRemoteSession) {
        try {
            Invoke-Command -Session $audioRemoteSession -ArgumentList $audioAcceptancePagePath -ScriptBlock {
                param($pagePath)
                $staticDirectory = [IO.Path]::GetFullPath('D:\PixelsServer\app\console-static')
                $resolvedPagePath = [IO.Path]::GetFullPath($pagePath)
                if ([IO.Path]::GetDirectoryName($resolvedPagePath).Equals($staticDirectory, [StringComparison]::OrdinalIgnoreCase) -and
                    [IO.Path]::GetFileName($resolvedPagePath).StartsWith('pixels-audio-acceptance-', [StringComparison]::Ordinal) -and
                    [IO.Path]::GetExtension($resolvedPagePath) -eq '.html') {
                    Remove-Item -LiteralPath $resolvedPagePath -Force -ErrorAction SilentlyContinue
                }
            }
        } catch {
            Write-Warning "Remote audio acceptance cleanup failed: $($_.Exception.Message)"
        } finally {
            Remove-PSSession $audioRemoteSession
        }
    }
    if ($fileRestartRemoteSession) {
        Remove-PSSession $fileRestartRemoteSession
    }
    if ($null -ne $previousTrustedHosts) {
        Set-Item -LiteralPath 'WSMan:\localhost\Client\TrustedHosts' -Value $previousTrustedHosts -Force
    }
    if ($acceptanceRoot) {
        $resolvedAcceptanceRoot = [IO.Path]::GetFullPath($acceptanceRoot)
        $resolvedTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if ($resolvedAcceptanceRoot.StartsWith($resolvedTempRoot, [StringComparison]::OrdinalIgnoreCase) -and
            [IO.Path]::GetFileName($resolvedAcceptanceRoot).StartsWith('pixels-file-transfer-', [StringComparison]::Ordinal)) {
            [IO.Directory]::Delete($resolvedAcceptanceRoot, $true)
        } else {
            Write-Warning 'File-transfer acceptance temporary path failed the cleanup boundary check.'
        }
    }
    $httpClient.Dispose()
    $httpHandler.Dispose()
    $trustedRoot.Dispose()
}
