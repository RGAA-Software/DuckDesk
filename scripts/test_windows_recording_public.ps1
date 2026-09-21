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
    [ValidateRange(30, 180)]
    [int]$RecordingTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$ConsoleBase = $ConsoleBase.TrimEnd('/')
$repository = Split-Path $PSScriptRoot -Parent
$credentialsPath = Join-Path $repository '.env/public_test_user.json'
$cloudApplicationTest = Join-Path $PSScriptRoot 'test_windows_cloud_app_public.ps1'
$recordingBrowserTest = Join-Path $PSScriptRoot 'test_windows_recording_browser_public.ps1'
foreach ($path in @($credentialsPath, $cloudApplicationTest, $recordingBrowserTest)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Windows recording acceptance input is missing: $path"
    }
}

$cloudApplicationResult = & $cloudApplicationTest `
    -ConsoleBase $ConsoleBase `
    -CertificateAuthority $CertificateAuthority `
    -ClientTimeoutSeconds 30 |
    Where-Object { $_.PSObject.Properties.Name -contains 'Result' } |
    Select-Object -Last 1
if (-not $cloudApplicationResult -or $cloudApplicationResult.Result -ne 'PASS') {
    throw 'The prerequisite public Windows cloud application acceptance did not pass.'
}
$resourceSessionId = [string]$cloudApplicationResult.ResourceSessionId

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
$credentials = Get-Content -LiteralPath $credentialsPath -Raw | ConvertFrom-Json
$accessToken = ''

function Send-ConsoleRequest {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [ValidateSet('GET', 'POST', 'DELETE')]
        [string]$Method = 'GET',
        [object]$Body = $null,
        [string]$Token = '',
        [switch]$UserResource,
        [switch]$Binary
    )

    $request = [Net.Http.HttpRequestMessage]::new(
        [Net.Http.HttpMethod]::new($Method),
        "$ConsoleBase$Path")
    try {
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
            if (-not $response.IsSuccessStatusCode) {
                throw "HTTP $([int]$response.StatusCode)"
            }
            if ($Binary) {
                return $response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
            }
            $content = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            if ([string]::IsNullOrWhiteSpace($content)) {
                return $null
            }
            return $content | ConvertFrom-Json
        }
        finally {
            $response.Dispose()
        }
    }
    catch {
        throw "Console API request failed: method=$Method path=$Path; $($_.Exception.Message)"
    }
    finally {
        $request.Dispose()
    }
}

try {
    $login = Send-ConsoleRequest -Path '/api/console/sessions' -Method POST -Body @{
        username = [string]$credentials.username
        password = [string]$credentials.password
    }
    $accessToken = [string]$login.token
    if ($accessToken -notmatch '^[0-9a-f]{64}$') {
        throw 'Windows recording acceptance login returned an invalid access token.'
    }

    $recording = $null
    $recordingDeadline = [DateTime]::UtcNow.AddSeconds($RecordingTimeoutSeconds)
    while (-not $recording -and [DateTime]::UtcNow -lt $recordingDeadline) {
        $recordings = @(Send-ConsoleRequest `
            -Path '/api/console/recordings?limit=100' `
            -Token $accessToken `
            -UserResource)
        $recording = $recordings |
            Where-Object { $_.session_id -eq $resourceSessionId -and [long]$_.size_bytes -gt 0 } |
            Select-Object -First 1
        if (-not $recording) {
            Start-Sleep -Seconds 2
        }
    }
    if (-not $recording) {
        throw "No finalized recording was reported for resource session $resourceSessionId."
    }

    $encodedRecordingId = [Uri]::EscapeDataString([string]$recording.id)
    $cache = $null
    $cacheDeadline = [DateTime]::UtcNow.AddSeconds($RecordingTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $cacheDeadline) {
        $cache = Send-ConsoleRequest `
            -Path "/api/console/recordings/$encodedRecordingId/cache" `
            -Method POST `
            -Token $accessToken `
            -UserResource
        if ($cache.state -eq 'ready') {
            break
        }
        if ($cache.state -eq 'retry_required') {
            throw 'Recording cache entered retry_required during public acceptance.'
        }
        Start-Sleep -Seconds 2
    }
    if (-not $cache -or $cache.state -ne 'ready') {
        throw "Recording cache did not become ready: state=$($cache.state)."
    }
    if ([long]$cache.size_bytes -ne [long]$recording.size_bytes -or
        [long]$cache.received_bytes -ne [long]$recording.size_bytes) {
        throw 'Recording cache size does not match the finalized recording.'
    }

    [byte[]]$recordingBytes = Send-ConsoleRequest `
        -Path "/api/console/recordings/$encodedRecordingId/download" `
        -Token $accessToken `
        -UserResource `
        -Binary
    if ($recordingBytes.Length -ne [long]$recording.size_bytes) {
        throw 'Downloaded recording size does not match the finalized recording.'
    }
    if ($recordingBytes.Length -lt 12 -or
        [Text.Encoding]::ASCII.GetString($recordingBytes, 4, 4) -ne 'ftyp') {
        throw 'Downloaded recording is not an MP4 file with an ftyp box.'
    }
    $downloadHash = [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($recordingBytes))

    $browserResult = & $recordingBrowserTest `
        -ConsoleBase $ConsoleBase `
        -CertificateAuthority $CertificateAuthority `
        -RecordingFileName ([string]$recording.file_name) `
        -RecordingSha256 $downloadHash `
        -RecordingSizeBytes ([long]$recording.size_bytes) |
        Where-Object { $_.PSObject.Properties.Name -contains 'Result' } |
        Select-Object -Last 1
    if (-not $browserResult -or $browserResult.Result -ne 'PASS') {
        throw 'The public Console recording browser acceptance did not pass.'
    }

    [pscustomobject]@{
        Result = 'PASS'
        ResourceSessionId = $resourceSessionId
        RecordingId = [string]$recording.id
        FileName = [string]$recording.file_name
        Codec = [string]$recording.codec
        SizeBytes = [long]$recording.size_bytes
        CacheState = [string]$cache.state
        DownloadSha256 = $downloadHash
        Mp4HeaderValid = $true
        AdminRetention = $true
        AdminRelease = $true
        AdminEviction = $true
        UserBrowserRedownload = $true
    }
}
finally {
    if ($accessToken) {
        try {
            [void](Send-ConsoleRequest -Path '/api/console/session' -Method DELETE -Token $accessToken)
        }
        catch {
            Write-Warning "Windows recording test login cleanup failed: $($_.Exception.Message)"
        }
    }
    $accessToken = ''
    $httpClient.Dispose()
    $httpHandler.Dispose()
    $trustedRoot.Dispose()
}
