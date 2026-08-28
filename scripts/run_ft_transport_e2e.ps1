param(
    [ValidateSet('ws', 'wss', 'relay', 'udp_direct')]
    [string]$Transport = 'ws',
    [ValidateRange(1, 1000)]
    [int]$Rounds = 1,
    [ValidateRange(0, 1073741824)]
    [int64]$Bytes = 16777216,
    [ValidateRange(0, 10000)]
    [int]$SmallFiles = 0,
    [ValidateSet('none', 'overwrite', 'skip')]
    [string]$ConflictMode = 'none',
    [ValidateRange(1000, 900000)]
    [int]$TimeoutMs = 300000,
    [string]$ConsoleBase = 'https://127.0.0.1:30500',
    [string]$TargetHost = '10.0.0.90',
    [ValidateRange(1, 65535)]
    [int]$TargetPort = 20371,
    [string]$DeviceId = '001190520',
    [string]$RemoteDir = 'C:/Windows/Temp',
    [switch]$RequireBusy,
    [string]$MongoExe = 'D:\software\mongodb_3.6\mongodb\bin\mongo.exe'
)

$ErrorActionPreference = 'Stop'
$suffix = [guid]::NewGuid().ToString('N').Substring(0, 10)
$username = "ft_transport_$suffix"
$password = "T!$([guid]::NewGuid().ToString('N'))"
$visitorDeviceId = "ftvisitor_$suffix"
$uid = $null
$pass = 0
$fail = 0
$failRounds = [Collections.Generic.List[int]]::new()
$started = Get-Date
$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot 'build_official\src\px_deps\px_client_sdk_new\test_ft_transport_e2e.exe'

if (-not (Test-Path -LiteralPath $testExe)) {
    throw "Missing test executable: $testExe"
}

if ($ConsoleBase.StartsWith('https://') -and
    -not (Get-Command Invoke-RestMethod).Parameters.ContainsKey('SkipCertificateCheck')) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    [Net.ServicePointManager]::ServerCertificateValidationCallback = { $true }
}
function Invoke-JsonPost([string]$Uri, [object]$Body, [string]$Bearer = '', [int]$Attempts = 1) {
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
        $request.SslProtocol = 'Tls12'
    }
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        try {
            return Invoke-RestMethod @request
        }
        catch {
            $status = [int]$_.Exception.Response.StatusCode
            $transientTransportError = $status -eq 0 -and (
                $_.Exception.Message -match 'SSL connection|connection.*(closed|reset|refused)|timed out')
            if ($transientTransportError -and $attempt -lt $Attempts) {
                Start-Sleep -Seconds 1
                continue
            }
            if ($status -ne 429 -or $attempt -eq $Attempts) {
                $detail = if ($_.ErrorDetails.Message) { $_.ErrorDetails.Message } else { '' }
                throw "POST $Uri failed: $($_.Exception.ToString()) $detail"
            }
            Start-Sleep -Seconds 15
        }
    }
}

$testEnvironmentNames = @(
    'PX_FT_E2E_TRANSPORT', 'PX_FT_E2E_HOST', 'PX_FT_E2E_PORT',
    'PX_FT_E2E_RELAY_HOST', 'PX_FT_E2E_RELAY_PORT',
    'PX_FT_E2E_REMOTE_DEVICE_ID', 'PX_FT_E2E_VISITOR_DEVICE_ID',
    'PX_FT_E2E_TICKET', 'PX_FT_E2E_NONCE', 'PX_FT_E2E_BYTES',
    'PX_FT_E2E_TIMEOUT_MS', 'PX_FT_E2E_REMOTE_DIR', 'PX_FT_E2E_REQUIRE_BUSY',
    'PX_FT_E2E_SMALL_FILES', 'PX_FT_E2E_CONFLICT_MODE'
)

try {
    $guest = Invoke-JsonPost "$ConsoleBase/api/v1/session/guest" `
        @{ client_nonce = "ft_guest_$suffix"; client_type = 'panel' } '' 9
    if ($guest.code -ne 200 -or -not $guest.data.access_token) {
        throw 'guest session failed'
    }
    $registered = Invoke-JsonPost "$ConsoleBase/api/v1/user/register" `
        @{ username = $username; password = $password } $guest.data.access_token 9
    if ($registered.code -ne 200 -or -not $registered.data.uid) {
        throw 'registration failed'
    }
    $uid = $registered.data.uid
    $login = Invoke-JsonPost "$ConsoleBase/api/v1/session/user/login" `
        @{ username = $username; password = $password; client_type = 'panel' } '' 9
    if ($login.code -ne 200 -or -not $login.data.access_token) {
        throw 'user login failed'
    }
    $accessToken = $login.data.access_token

    $qtBin = 'C:\Qt6.8.3\6.8.3\msvc2022_64\bin'
    $dist = Join-Path $repoRoot 'build_official\dist'
    $env:PATH = "$dist;$dist\deps;$dist\deps\ct_plugins;$qtBin;$env:PATH"

    for ($round = 1; $round -le $Rounds; $round++) {
        $roundStart = Get-Date
        $nonce = "ft_${Transport}_${suffix}_$round"
        try {
            $issued = Invoke-JsonPost "$ConsoleBase/api/v1/user/devices/$DeviceId/ticket" `
                @{ client_nonce = $nonce; requested_permissions = @('view', 'file') } `
                $accessToken 9
            if ($issued.code -ne 200 -or -not $issued.data.ticket) {
                throw 'ticket issue failed'
            }
            if ($Transport -eq 'relay' -and
                (-not $issued.data.relay_host -or [int]$issued.data.relay_port -le 0)) {
                throw 'Console did not issue a Relay endpoint'
            }

            $env:PX_FT_E2E_TRANSPORT = $Transport
            $env:PX_FT_E2E_HOST = $TargetHost
            $env:PX_FT_E2E_PORT = [string]$TargetPort
            $env:PX_FT_E2E_RELAY_HOST = [string]$issued.data.relay_host
            $env:PX_FT_E2E_RELAY_PORT = [string]$issued.data.relay_port
            $env:PX_FT_E2E_REMOTE_DEVICE_ID = $DeviceId
            $env:PX_FT_E2E_VISITOR_DEVICE_ID = $visitorDeviceId
            $env:PX_FT_E2E_TICKET = [string]$issued.data.ticket
            $env:PX_FT_E2E_NONCE = $nonce
            $env:PX_FT_E2E_BYTES = [string]$Bytes
            $env:PX_FT_E2E_SMALL_FILES = [string]$SmallFiles
            $env:PX_FT_E2E_CONFLICT_MODE = $ConflictMode
            $env:PX_FT_E2E_TIMEOUT_MS = [string]$TimeoutMs
            $env:PX_FT_E2E_REMOTE_DIR = $RemoteDir
            $env:PX_FT_E2E_REQUIRE_BUSY = if ($RequireBusy) { '1' } else { '0' }

            & $testExe --gtest_color=no
            if ($LASTEXITCODE -ne 0) {
                throw "native transport test exited with $LASTEXITCODE"
            }
            $pass++
            Write-Host ("FT_ROUND {0:D3}/{1} PASS transport={2} elapsed={3:N1}s" -f `
                $round, $Rounds, $Transport, ((Get-Date) - $roundStart).TotalSeconds)
        }
        catch {
            $fail++
            $failRounds.Add($round)
            Write-Host ("FT_ROUND {0:D3}/{1} FAIL transport={2} elapsed={3:N1}s reason={4}" -f `
                $round, $Rounds, $Transport, ((Get-Date) - $roundStart).TotalSeconds,
                $_.Exception.Message)
        }
        finally {
            foreach ($name in $testEnvironmentNames) {
                Remove-Item "Env:\$name" -ErrorAction SilentlyContinue
            }
        }
    }
}
finally {
    foreach ($name in $testEnvironmentNames) {
        Remove-Item "Env:\$name" -ErrorAction SilentlyContinue
    }
    if ($uid -and $uid -match '^[A-Za-z0-9_-]+$' -and (Test-Path -LiteralPath $MongoExe)) {
        $cleanup = @"
var u='$uid';
var registration=db.c_event.findOne({action:'user_register',target_id:u,result:'success'});
if(registration){db.c_user_session.deleteMany({subject_id:registration.actor_id});}
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

Write-Host ("FT_TRANSPORT_SUMMARY transport={0} pass={1} fail={2} failRounds={3} totalMinutes={4:N1}" -f `
    $Transport, $pass, $fail, ($failRounds -join ','), ((Get-Date) - $started).TotalMinutes)
if ($fail -gt 0) { exit 1 }
