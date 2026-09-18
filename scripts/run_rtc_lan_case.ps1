param(
    [ValidateSet('control', 'observe')]
    [string]$JoinMode = 'control',
    [ValidateRange(6, 600)]
    [int]$SampleSeconds = 15,
    [ValidateRange(5, 60)]
    [int]$ConnectTimeoutSeconds = 30,
    [ValidateRange(0, 100)]
    [double]$MaxLossRatePercent = 0,
    [string]$ConsoleBase = 'https://127.0.0.1:4600',
    [string]$DeviceId = '001190520',
    [string]$InstanceId = '',
    [ValidateSet('cdp_webrtc_diag.mjs', 'cdp_virtual_display_e2e.mjs', 'cdp_game_hook_input.mjs')]
    [string]$DiagnosticScript = 'cdp_webrtc_diag.mjs',
    [ValidateSet('default', 'accept', 'reject')]
    [string]$TakeoverConfirmation = 'default',
    [string]$EvidenceDir = '',
    [switch]$Quiet,
    [string]$BearerToken = '',
    [string]$MongoExe = 'D:\software\mongodb_3.6\mongodb\bin\mongo.exe'
)

$ErrorActionPreference = 'Stop'
$suffix = [guid]::NewGuid().ToString('N').Substring(0, 10)
$username = "rtc_$suffix"
$password = "T!$([guid]::NewGuid().ToString('N'))"
$uid = $null
$exitCode = 1

if ($ConsoleBase.StartsWith('https://') -and
    -not (Get-Command Invoke-RestMethod).Parameters.ContainsKey('SkipCertificateCheck')) {
    # Windows PowerShell 5.1 has no per-request switch. This callback is scoped
    # to this short-lived acceptance-test process and permits the bundled cert.
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    [Net.ServicePointManager]::ServerCertificateValidationCallback = { $true }
}

function Invoke-JsonPost([string]$Uri, [object]$Body, [string]$Bearer = '') {
    $headers = @{}
    if ($Bearer) { $headers.Authorization = "Bearer $Bearer" }
    $request = @{
        Method      = 'Post'
        Uri         = $Uri
        Headers     = $headers
        ContentType = 'application/json'
        Body        = ($Body | ConvertTo-Json -Compress -Depth 12)
        TimeoutSec  = 30
    }
    if ($Uri.StartsWith('https://') -and
        (Get-Command Invoke-RestMethod).Parameters.ContainsKey('SkipCertificateCheck')) {
        $request.SkipCertificateCheck = $true
    }
    Invoke-RestMethod @request
}

function ConvertTo-Base64Url([string]$Value) {
    [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Value)).TrimEnd('=').Replace('+', '-').Replace('/', '_')
}

try {
    $accessToken = $BearerToken
    if (-not $accessToken) {
        $guest = Invoke-JsonPost "$ConsoleBase/api/v1/session/guest" `
            @{ client_nonce = "guest_$suffix"; client_type = 'panel' }
        if ($guest.code -ne 200 -or -not $guest.data.access_token) { throw 'guest session failed' }

        $registered = Invoke-JsonPost "$ConsoleBase/api/v1/user/register" `
            @{ username = $username; password = $password } $guest.data.access_token
        if ($registered.code -ne 200 -or -not $registered.data.uid) { throw 'registration failed' }
        $uid = $registered.data.uid

        $login = Invoke-JsonPost "$ConsoleBase/api/v1/session/user/login" `
            @{ username = $username; password = $password; client_type = 'panel' }
        if ($login.code -ne 200 -or -not $login.data.access_token) { throw 'user login failed' }
        $accessToken = $login.data.access_token
    }

    $nonce = "rtc_direct_$suffix"
    $connectionPath = if ($InstanceId) {
        "/api/v1/user/instances/$([Uri]::EscapeDataString($InstanceId))/web-connection"
    } else {
        "/api/v1/user/devices/$([Uri]::EscapeDataString($DeviceId))/web-connection"
    }
    $connection = Invoke-JsonPost "$ConsoleBase$connectionPath" `
        @{ client_nonce = $nonce; join_mode = $JoinMode } `
        $accessToken
    if ($connection.code -ne 200 -or -not $connection.data.launch_url -or -not $connection.data.password_hash) {
        throw 'web connection resolution failed'
    }
    $value = $connection.data
    $actualPermissions = @($value.permissions | ForEach-Object { [string]$_ } | Sort-Object -Unique)
    $expectedPermissions = if ($JoinMode -eq 'observe') {
        @('audio', 'view')
    } elseif ($InstanceId) {
        @('audio', 'clipboard', 'input', 'view')
    } else {
        @('audio', 'clipboard', 'file', 'input', 'view')
    }
    if (($actualPermissions -join ',') -ne ($expectedPermissions -join ',')) {
        throw "unexpected permissions for join=$JoinMode instance=$InstanceId`: actual=$($actualPermissions -join ',') expected=$($expectedPermissions -join ',')"
    }

    $launch = [uri]$value.launch_url
    $query = [Web.HttpUtility]::ParseQueryString($launch.Query)
    $query['connType'] = 'rtc_direct'
    $query['stream_id'] = $value.stream_id
    $query['c'] = ConvertTo-Base64Url (@{d=$value.device_id; m=$value.password_hash} | ConvertTo-Json -Compress)
    $fragment = [Web.HttpUtility]::ParseQueryString($launch.Fragment.TrimStart('#'))
    $fragment['perms'] = $value.permissions -join ','
    $builder = [UriBuilder]::new($launch)
    $builder.Query = $query.ToString()
    $builder.Fragment = $fragment.ToString()

    $env:WEB_URL = $builder.Uri.AbsoluteUri
    $env:SAMPLE_SECONDS = [string]$SampleSeconds
    $env:CONNECT_TIMEOUT_SECONDS = [string]$ConnectTimeoutSeconds
    $env:EXPECT_CANDIDATE_TYPE = 'host'
    $env:TAKEOVER_CONFIRMATION = $TakeoverConfirmation
    $env:EXPECT_INPUT = if ($JoinMode -eq 'observe') { 'disabled' } else { 'enabled' }
    $env:MAX_LOSS_RATE_PERCENT = [string]$MaxLossRatePercent
    $env:RENDER_PORT = [string]$launch.Port
    # A fresh port prevents a detached Chrome from a previous interrupted run
    # from accepting CDP HTTP requests while no longer servicing commands.
    $env:CDP_PORT = [string](Get-Random -Minimum 22000 -Maximum 45000)
    if ($EvidenceDir) { $env:OUT_DIR = $EvidenceDir }
    if ($Quiet) { $env:QUIET = '1' }
    if (-not $Quiet) { Write-Host "Running Direct Host RTC gate: join=$JoinMode candidate=host samples=${SampleSeconds}s" }
    $nodeStarted = Get-Date
    & node (Join-Path $PSScriptRoot $DiagnosticScript)
    $nodeExitCode = $LASTEXITCODE
    if ($nodeExitCode -ne 0) { throw "RTC diagnostic exited with $nodeExitCode" }
    if ($DiagnosticScript -eq 'cdp_webrtc_diag.mjs' -and
        ((Get-Date) - $nodeStarted).TotalSeconds -lt $SampleSeconds) {
        throw 'RTC diagnostic exited before completing the requested sample duration'
    }
    $exitCode = 0
}
finally {
    foreach ($name in 'WEB_URL', 'SAMPLE_SECONDS', 'CONNECT_TIMEOUT_SECONDS', 'EXPECT_CANDIDATE_TYPE', 'TAKEOVER_CONFIRMATION', 'EXPECT_INPUT', 'MAX_LOSS_RATE_PERCENT', 'RENDER_PORT', 'CDP_PORT', 'OUT_DIR', 'QUIET') {
        Remove-Item "Env:\$name" -ErrorAction SilentlyContinue
    }
    if ($uid -and $uid -match '^[A-Za-z0-9_-]+$' -and (Test-Path -LiteralPath $MongoExe)) {
        $cleanup = @"
var u='$uid';
var registration=db.c_event.findOne({action:'user_register',target_id:u,result:'success'});
if(registration){db.c_user_session.deleteMany({subject_id:registration.actor_id});}
db.c_user_session.deleteMany({subject_id:u});
db.c_user_group_member.deleteMany({uid:u});
db.c_user_device.deleteMany({uid:u});
db.c_user.deleteMany({uid:u});
db.c_event.deleteMany({`$or:[{actor_id:u},{target_id:u}]});
printjson({users:db.c_user.count({uid:u}),sessions:db.c_user_session.count({subject_id:u})});
"@
        $cleanupResult = & $MongoExe db_gr_console_server --quiet --eval $cleanup
        if (-not $Quiet) { $cleanupResult }
    }
}

exit $exitCode
