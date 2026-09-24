#requires -Version 7.0

[CmdletBinding()]
param(
    [ValidateScript({
        $candidate = $null
        [Uri]::TryCreate($_, [UriKind]::Absolute, [ref]$candidate) -and
            $candidate.Scheme -eq 'https' -and
            -not $candidate.Query -and
            -not $candidate.Fragment
    })]
    [string]$ConsoleBase = 'https://39.71.45.66:4600',
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$CertificateAuthority = '.env/public_console_ca.pem',
    [string]$AppId = '',
    [string]$WebAssetRoot = '',
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$BrowserPath = 'C:/Program Files/Google/Chrome/Application/chrome.exe',
    [ValidateRange(15, 180)]
    [int]$StartTimeoutSeconds = 90,
    [ValidateRange(15, 180)]
    [int]$BrowserTimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
$ConsoleBase = $ConsoleBase.TrimEnd('/')
$repository = Split-Path $PSScriptRoot -Parent
$credentialsPath = Join-Path $repository '.env/public_test_user.json'
$browserProbePath = Join-Path $PSScriptRoot 'test_web_cloud_app_browser.mjs'
$webAssetRoot = if ($WebAssetRoot) {
    [IO.Path]::GetFullPath($WebAssetRoot)
} else {
    Join-Path $repository 'build_official/cloud_node/dist/web_client'
}

foreach ($requiredPath in @($CertificateAuthority, $credentialsPath, $browserProbePath, (Join-Path $webAssetRoot 'index.html'))) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Web public acceptance input is missing: $requiredPath"
    }
}

$trustedRoot = [Security.Cryptography.X509Certificates.X509Certificate2]::CreateFromPem(
    [IO.File]::ReadAllText((Resolve-Path -LiteralPath $CertificateAuthority).Path))
$certificatePolicy = [Security.Cryptography.X509Certificates.X509ChainPolicy]::new()
$certificatePolicy.TrustMode = [Security.Cryptography.X509Certificates.X509ChainTrustMode]::CustomRootTrust
[void]$certificatePolicy.CustomTrustStore.Add($trustedRoot)
$certificatePolicy.RevocationMode = [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
$certificatePolicy.VerificationFlags = [Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
$httpHandler = [Net.Http.SocketsHttpHandler]::new()
$httpHandler.SslOptions.CertificateChainPolicy = $certificatePolicy
$httpClient = [Net.Http.HttpClient]::new($httpHandler)
$httpClient.Timeout = [TimeSpan]::FromSeconds(20)

$credentials = Get-Content -LiteralPath $credentialsPath -Raw | ConvertFrom-Json
$accessToken = ''
$applicationInstance = $null
$resourceSession = $null

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

    $request = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::new($Method), "$ConsoleBase$Path")
    try {
        [void]$request.Headers.TryAddWithoutValidation('Accept', 'application/json')
        [void]$request.Headers.TryAddWithoutValidation('Origin', ([Uri]$ConsoleBase).GetLeftPart([UriPartial]::Authority))
        [void]$request.Headers.TryAddWithoutValidation('X-Pixels-Client-Type', 'user_web')
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

try {
    $login = Invoke-ConsoleApi -Path '/api/console/sessions' -Method POST -Body @{
        username = [string]$credentials.username
        password = [string]$credentials.password
    }
    $accessToken = [string]$login.token
    if ($accessToken -notmatch '^[0-9a-f]{64}$') {
        throw 'Web user login returned an invalid access token.'
    }

    $applications = @(Invoke-ConsoleApi -Path '/api/console/applications?limit=100' -Token $accessToken)
    $application = if ($AppId) {
        $applications | Where-Object { $_.id -eq $AppId } | Select-Object -First 1
    } else {
        $applications | Where-Object { $_.kind -eq 'webview' } | Select-Object -First 1
    }
    if (-not $application) {
        throw 'No matching WebView cloud application is available to the web test user.'
    }

    $instanceRequestId = [guid]::NewGuid().ToString()
    $createDeadline = [DateTime]::UtcNow.AddSeconds($StartTimeoutSeconds)
    while (-not $applicationInstance) {
        try {
            $applicationInstance = Invoke-ConsoleApi -Path '/api/console/instances' -Method POST -Body @{
                request_id = $instanceRequestId
                application_id = [string]$application.id
                deployment_id = $null
            } -Token $accessToken -UserResource
        }
        catch {
            if ($_.Exception.Message -notmatch 'HTTP 503' -or [DateTime]::UtcNow -ge $createDeadline) {
                throw
            }
            Start-Sleep -Milliseconds 500
        }
    }

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
        $descriptor.session.client_type -ne 'user_web' -or $descriptor.session.target.kind -ne 'cloud_application' -or
        $descriptor.session.target.application_id -ne $application.id -or
        $descriptor.session.target.instance_id -ne $applicationInstance.id -or
        $descriptor.session.id -ne $requestedSessionId -or [long]$descriptor.session.revision -le 0 -or
        $frontendToken -notmatch '^[0-9a-f]{64}$') {
        throw 'Console returned an invalid PostgreSQL Web connection descriptor.'
    }

    $launch = [UriBuilder]::new('http', [string]$descriptor.host, [int]$descriptor.port, '/web/')
    $launch.Query = "deviceId=$([Uri]::EscapeDataString([string]$applicationInstance.id))&stream_id=$([Uri]::EscapeDataString([string]$resourceSession.id))&instanceId=$([Uri]::EscapeDataString([string]$applicationInstance.id))&force420=1"
    $launch.Fragment = "session_id=$([Uri]::EscapeDataString([string]$resourceSession.id))&session_revision=$([long]$resourceSession.revision)&frontend_token=$frontendToken&perms=view%2Cinput%2Cclipboard%2Cfile%2Caudio"
    $env:PIXELS_WEB_ACCEPTANCE_URL = $launch.Uri.AbsoluteUri
    $env:PIXELS_WEB_ACCEPTANCE_TIMEOUT_MS = [string]($BrowserTimeoutSeconds * 1000)
    $env:PIXELS_WEB_ACCEPTANCE_BROWSER = (Resolve-Path -LiteralPath $BrowserPath).Path
    $env:PIXELS_WEB_ACCEPTANCE_ASSET_ROOT = (Resolve-Path -LiteralPath $webAssetRoot).Path
    $env:PIXELS_WEB_ACCEPTANCE_RENDER_ORIGIN = "http://$($descriptor.host):$($descriptor.port)"
    $browserOutput = & node $browserProbePath
    if ($LASTEXITCODE -ne 0) {
        throw "Chromium Web Client acceptance failed with exit code $LASTEXITCODE."
    }
    $browserEvidence = $browserOutput | ConvertFrom-Json
    if ($browserEvidence.result -ne 'PASS') {
        throw 'Chromium Web Client acceptance did not return PASS.'
    }

    [pscustomobject]@{
        Result = 'PASS'
        Mode = 'WebRTC Direct Host'
        ClientType = [string]$descriptor.session.client_type
        AppId = [string]$application.id
        AppType = [string]$application.kind
        InstanceState = [string]$applicationInstance.state
        ResourceSessionId = [string]$resourceSession.id
        Endpoint = "$($descriptor.host):$($descriptor.port)"
        PeerConnection = [string]$browserEvidence.connectionState
        SctpState = [string]$browserEvidence.sctpState
        VideoSize = "$($browserEvidence.videoWidth)x$($browserEvidence.videoHeight)"
        DecodedFrames = [long]$browserEvidence.videoFrames
        ReceivedVideoBytes = [long]$browserEvidence.videoBytes
        ReceivedAudioBytes = [long]$browserEvidence.audioBytes
        FrontendTokenRemovedFromVisibleUrl = -not [bool]$browserEvidence.visibleUrlContainsFrontendToken
    }
} finally {
    Remove-Item Env:PIXELS_WEB_ACCEPTANCE_URL -ErrorAction SilentlyContinue
    Remove-Item Env:PIXELS_WEB_ACCEPTANCE_TIMEOUT_MS -ErrorAction SilentlyContinue
    Remove-Item Env:PIXELS_WEB_ACCEPTANCE_BROWSER -ErrorAction SilentlyContinue
    Remove-Item Env:PIXELS_WEB_ACCEPTANCE_ASSET_ROOT -ErrorAction SilentlyContinue
    Remove-Item Env:PIXELS_WEB_ACCEPTANCE_RENDER_ORIGIN -ErrorAction SilentlyContinue
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
            Write-Warning "Web test login cleanup failed: $($_.Exception.Message)"
        }
    }
    $accessToken = ''
    $frontendToken = ''
    $httpClient.Dispose()
    $httpHandler.Dispose()
    $trustedRoot.Dispose()
}
