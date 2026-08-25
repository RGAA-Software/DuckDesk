param(
    [ValidateSet('host', 'relay')]
    [string]$ExpectedCandidate = 'host',
    [ValidateSet('', 'udp', 'tcp')]
    [string]$ExpectedRelayProtocol = '',
    [switch]$BlockDirectUdp,
    [switch]$BlockTurnUdp,
    [ValidateRange(6, 600)]
    [int]$SampleSeconds = 15,
    [string]$ConsoleBase = 'https://127.0.0.1:30500',
    [string]$TargetHost = '10.0.0.90',
    [string]$DeviceId = '001190520',
    [ValidateSet('cdp_webrtc_diag.mjs', 'cdp_virtual_display_e2e.mjs', 'cdp_game_hook_input.mjs')]
    [string]$DiagnosticScript = 'cdp_webrtc_diag.mjs',
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
$rules = [Collections.Generic.List[string]]::new()
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

function Add-BlockRule([string]$Suffix, [string]$RemoteAddress, [string]$RemotePort = 'Any') {
    $name = "PixelsAcceptance_$PID`_$suffix`_$Suffix"
    New-NetFirewallRule -Name $name -DisplayName $name -Direction Outbound -Action Block `
        -Protocol UDP -RemoteAddress $RemoteAddress -RemotePort $RemotePort | Out-Null
    $rules.Add($name)
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

    if ($BlockDirectUdp) { Add-BlockRule 'direct_udp' $TargetHost }

    $nonce = "standard_$suffix"
    $ticket = Invoke-JsonPost "$ConsoleBase/api/v1/user/devices/$DeviceId/ticket" `
        @{ client_nonce = $nonce; requested_permissions = @('view', 'input', 'clipboard', 'file', 'audio') } `
        $accessToken
    if ($ticket.code -ne 200 -or -not $ticket.data.ticket) { throw 'ticket issue failed' }
    $value = $ticket.data

    if ($BlockTurnUdp) { Add-BlockRule 'turn_udp' $value.relay_host ([string]$value.rtc_ice_config.ice_servers[0].urls[0].Split(':')[-1].Split('?')[0]) }

    $launch = [uri]$value.launch_url
    $query = [Web.HttpUtility]::ParseQueryString($launch.Query)
    $query['connType'] = 'rtc'
    $fragment = [Web.HttpUtility]::ParseQueryString($launch.Fragment.TrimStart('#'))
    $fragment['renew_url'] = "$ConsoleBase/api/v1/connection-tickets/renew"
    $fragment['renew'] = $value.renewal_token
    $fragment['perms'] = $value.permissions -join ','
    $fragment['relay_host'] = $value.relay_host
    $fragment['relay_port'] = [string]$value.relay_port
    $fragment['signal_device_id'] = $value.signal_device_id
    $fragment['ice'] = ConvertTo-Base64Url ($value.rtc_ice_config | ConvertTo-Json -Compress -Depth 12)
    $builder = [UriBuilder]::new($launch)
    $builder.Query = $query.ToString()
    $builder.Fragment = $fragment.ToString()

    $env:WEB_URL = $builder.Uri.AbsoluteUri
    $env:SAMPLE_SECONDS = [string]$SampleSeconds
    $env:EXPECT_CANDIDATE_TYPE = $ExpectedCandidate
    $env:EXPECT_RELAY_PROTOCOL = $ExpectedRelayProtocol
    $env:FORCE_RELAY = if ($ExpectedCandidate -eq 'relay') { '1' } else { '0' }
    $env:RENDER_PORT = [string]$launch.Port
    # A fresh port prevents a detached Chrome from a previous interrupted run
    # from accepting CDP HTTP requests while no longer servicing commands.
    $env:CDP_PORT = [string](Get-Random -Minimum 22000 -Maximum 45000)
    if ($EvidenceDir) { $env:OUT_DIR = $EvidenceDir }
    if ($Quiet) { $env:QUIET = '1' }
    if (-not $Quiet) { Write-Host "Running RTC LAN gate: candidate=$ExpectedCandidate relayProtocol=$ExpectedRelayProtocol samples=${SampleSeconds}s" }
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
    foreach ($name in $rules) {
        Remove-NetFirewallRule -Name $name -ErrorAction SilentlyContinue
    }
    foreach ($name in 'WEB_URL', 'SAMPLE_SECONDS', 'EXPECT_CANDIDATE_TYPE', 'EXPECT_RELAY_PROTOCOL', 'FORCE_RELAY', 'RENDER_PORT', 'CDP_PORT', 'OUT_DIR', 'QUIET') {
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
        $cleanupResult = & $MongoExe db_gr_console_server --quiet --eval $cleanup
        if (-not $Quiet) { $cleanupResult }
    }
    $remainingRules = @($rules | Where-Object { Get-NetFirewallRule -Name $_ -ErrorAction SilentlyContinue })
    if ($remainingRules.Count -ne 0) {
        Write-Error "acceptance firewall cleanup failed: $($remainingRules -join ',')"
        $exitCode = 1
    }
}

exit $exitCode
