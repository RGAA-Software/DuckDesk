param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('account', 'guest')]
    [string]$Mode,
    [string]$ConsoleBase = 'https://127.0.0.1:30500',
    [string]$TargetHost = '10.0.0.90',
    [int]$TargetPort = 20371,
    [string]$DeviceId = '001190520',
    [ValidateSet('webrtc_direct', 'webrtc')]
    [string]$NetworkType = 'webrtc_direct',
    [string]$RemotePassword = $env:PX_TEST_REMOTE_PASSWORD,
    [string]$ClientExe = '',
    [switch]$SplitWindows,
    [ValidateRange(1, 8)]
    [int]$MaxScreens = 4,
    [ValidateRange(1, 8)]
    [int]$ExpectedMonitorCount = 1,
    [ValidateRange(10, 90)]
    [int]$TimeoutSeconds = 40,
    [string]$MongoExe = 'D:\software\mongodb_3.6\mongodb\bin\mongo.exe'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
if (-not $ClientExe) {
    $ClientExe = Join-Path $repoRoot 'build_official\dist\px_client.exe'
}
$ClientExe = (Resolve-Path -LiteralPath $ClientExe).Path
$requiredDist = (Resolve-Path -LiteralPath (Join-Path $repoRoot 'build_official\dist')).Path
if (-not $ClientExe.StartsWith($requiredDist, [StringComparison]::OrdinalIgnoreCase)) {
    throw "native acceptance must use build_official\\dist: $ClientExe"
}

if ($ConsoleBase.StartsWith('https://') -and
    -not (Get-Command Invoke-RestMethod).Parameters.ContainsKey('SkipCertificateCheck')) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    [Net.ServicePointManager]::ServerCertificateValidationCallback = { $true }
}

function Invoke-JsonPost([string]$Uri, [object]$Body, [string]$Bearer = '') {
    $headers = @{}
    if ($Bearer) { $headers.Authorization = "Bearer $Bearer" }
    $request = @{
        Method = 'Post'; Uri = $Uri; Headers = $headers
        ContentType = 'application/json'
        Body = ($Body | ConvertTo-Json -Compress -Depth 12)
        TimeoutSec = 30
    }
    if ($Uri.StartsWith('https://') -and
        (Get-Command Invoke-RestMethod).Parameters.ContainsKey('SkipCertificateCheck')) {
        $request.SkipCertificateCheck = $true
    }
    Invoke-RestMethod @request
}

function ConvertTo-Base64([string]$Value) {
    [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Value))
}

$suffix = [guid]::NewGuid().ToString('N').Substring(0, 10)
$streamId = "native_${Mode}_$suffix"
$clientId = "acceptance_$suffix"
$uid = $null
$process = $null
$exitCode = 1
$logPath = if ($NetworkType -eq 'webrtc') {
    "C:\Users\Public\Pixels\px_logs\app.$DeviceId.log"
} else {
    "C:\Users\Public\Pixels\px_logs\app.$TargetHost.log"
}
$rtcLogPath = "C:\Users\Public\Pixels\px_logs\app.rtc.$DeviceId.log"
$initialLines = if (Test-Path -LiteralPath $logPath) {
    @(Get-Content -LiteralPath $logPath).Count
} else { 0 }
$initialRtcLines = if (Test-Path -LiteralPath $rtcLogPath) {
    @(Get-Content -LiteralPath $rtcLogPath).Count
} else { 0 }

try {
    $nonce = "native_$suffix"
    $connectionTicket = ''
    $relayHost = ([uri]$ConsoleBase).Host
    $relayPort = 30502

    if ($Mode -eq 'account') {
        $username = "native_$suffix"
        $password = "T!$([guid]::NewGuid().ToString('N'))"
        $guest = Invoke-JsonPost "$ConsoleBase/api/v1/session/guest" `
            @{ client_nonce = "guest_$suffix"; client_type = 'panel' }
        $registered = Invoke-JsonPost "$ConsoleBase/api/v1/user/register" `
            @{ username = $username; password = $password } $guest.data.access_token
        $uid = $registered.data.uid
        $login = Invoke-JsonPost "$ConsoleBase/api/v1/session/user/login" `
            @{ username = $username; password = $password; client_type = 'panel' }
        $issued = Invoke-JsonPost "$ConsoleBase/api/v1/user/devices/$DeviceId/ticket" `
            @{ client_nonce = $nonce; requested_permissions = @('view', 'input', 'clipboard', 'file', 'audio') } `
            $login.data.access_token
        $connectionTicket = $issued.data.ticket
        $relayHost = $issued.data.relay_host
        $relayPort = [int]$issued.data.relay_port
        if (-not $connectionTicket) { throw 'account ticket issue returned an empty ticket' }
    } elseif (-not $RemotePassword) {
        throw 'guest mode requires -RemotePassword or PX_TEST_REMOTE_PASSWORD'
    }

    $arguments = @(
        "--host=$TargetHost", "--port=$TargetPort",
        "--console_host=$(([uri]$ConsoleBase).Host)", "--console_port=$(([uri]$ConsoleBase).Port)",
        '--console_ssl=true', '--audio=1', '--clipboard=1',
        "--stream_id=$streamId", "--conn_type=$(if ($Mode -eq 'account') {'console_ticket'} else {'direct'})",
        "--network_type=$NetworkType", "--device_id=$clientId",
        "--remote_device_id=$DeviceId", '--enable_p2p=1', '--only_viewing=0',
        "--split_windows=$($SplitWindows.IsPresent.ToString().ToLowerInvariant())",
        "--max_num_of_screen=$MaxScreens",
        "--force_direct=$(if ($NetworkType -eq 'webrtc_direct') {1} else {0})",
        "--relay_host=$relayHost", "--relay_port=$relayPort"
    )
    if ($Mode -eq 'account') {
        $arguments += "--connection_ticket=$(ConvertTo-Base64 $connectionTicket)"
        $arguments += "--connection_nonce=$nonce"
    } else {
        $arguments += "--remote_device_rp=$(ConvertTo-Base64 $RemotePassword)"
    }

    $process = Start-Process -FilePath $ClientExe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $ClientExe -Parent) -WindowStyle Hidden -PassThru

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $evidence = ''
    do {
        Start-Sleep -Milliseconds 500
        if ($process.HasExited) { break }
        if (Test-Path -LiteralPath $logPath) {
            $evidence = (@(Get-Content -LiteralPath $logPath) | Select-Object -Skip $initialLines) -join "`n"
            if ($NetworkType -eq 'webrtc' -and (Test-Path -LiteralPath $rtcLogPath)) {
                $rtcEvidence = (@(Get-Content -LiteralPath $rtcLogPath) | Select-Object -Skip $initialRtcLines) -join "`n"
                $evidence += "`n$rtcEvidence"
            }
            $transportReady = if ($NetworkType -eq 'webrtc_direct') {
                $evidence -match 'Rtc local, connected\.' -and
                $evidence -match 'first key frame'
            } else {
                $evidence -match 'Full WebRTC transport is ready' -and
                $evidence -match 'RtcVideoSink OnFrame #1'
            }
            $renderViewsReady = $ExpectedMonitorCount -le 1 -or
                $evidence -match "Render view pool expanded on demand: requested=$ExpectedMonitorCount, active_capacity=$ExpectedMonitorCount"
            $keyFrameCount = ([regex]::Matches($evidence, 'first key frame')).Count
            if ($transportReady -and $renderViewsReady -and
                $keyFrameCount -ge $ExpectedMonitorCount -and
                $evidence -match 'First decoded video frame reached UI renderer' -and
                $evidence -match 'File-transfer transport connected' -and
                $evidence -match 'Init audio player') {
                break
            }
        }
    } while ((Get-Date) -lt $deadline)

    if ($process.HasExited) { throw "native client exited before acceptance, code=$($process.ExitCode)" }
    if ($evidence -match '(?i)authentication required|resource not found|ticket.+failed|connect failed') {
        throw 'native client log contains an authentication or connection failure'
    }
    if ($NetworkType -eq 'webrtc_direct') {
        if ($evidence -notmatch 'Rtc local, connected\.') { throw 'native Direct RTC did not connect' }
        if ($evidence -notmatch 'first key frame') { throw 'native Direct RTC did not decode the first key frame' }
    } else {
        if ($evidence -notmatch 'Full WebRTC transport is ready') { throw 'native standard RTC did not connect' }
        if ($evidence -notmatch 'RtcVideoSink OnFrame #1') { throw 'native standard RTC sink did not receive the first frame' }
    }
    if ($evidence -notmatch 'First decoded video frame reached UI renderer') {
        throw 'native client decoded video but did not deliver it to the UI renderer'
    }
    if ($ExpectedMonitorCount -gt 1 -and
        $evidence -notmatch "Render view pool expanded on demand: requested=$ExpectedMonitorCount, active_capacity=$ExpectedMonitorCount") {
        throw "native render view pool did not expand to $ExpectedMonitorCount monitors"
    }
    if (([regex]::Matches($evidence, 'first key frame')).Count -lt $ExpectedMonitorCount) {
        throw "native RTC did not receive a first key frame from all $ExpectedMonitorCount monitor tracks"
    }
    if ($evidence -notmatch 'File-transfer transport connected') { throw 'native file transport did not connect' }
    if ($evidence -notmatch 'Init audio player') { throw 'native audio player did not initialize' }

    Write-Host "NATIVE_AUTH_CASE PASS mode=$Mode route=$NetworkType target=$DeviceId monitors=$ExpectedMonitorCount rtc=connected video=ui-rendered audio=initialized file=connected"
    $exitCode = 0
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    if ($uid -and $uid -match '^[A-Za-z0-9_-]+$' -and (Test-Path -LiteralPath $MongoExe)) {
        $cleanup = @"
var u='$uid';
db.c_connection_ticket.deleteMany({subject_id:u});
db.c_user_session.deleteMany({subject_id:u});
db.c_user_group_member.deleteMany({uid:u});
db.c_user_device.deleteMany({uid:u});
db.c_user.deleteMany({uid:u});
db.c_event.deleteMany({`$or:[{actor_id:u},{target_id:u}]});
printjson({users:db.c_user.count({uid:u}),sessions:db.c_user_session.count({subject_id:u}),tickets:db.c_connection_ticket.count({subject_id:u})});
"@
        & $MongoExe db_gr_console_server --quiet --eval $cleanup
    }
}

exit $exitCode
