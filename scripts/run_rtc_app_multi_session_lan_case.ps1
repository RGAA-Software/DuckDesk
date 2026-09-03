param(
    [Parameter(Mandatory = $true)]
    [string]$AppId,
    [ValidateSet('rtc', 'rtc_direct')]
    [string]$ConnectionMode = 'rtc_direct',
    [ValidateSet('any', 'host', 'relay')]
    [string]$ExpectedCandidate = 'host',
    [string]$ConsoleBase = 'https://127.0.0.1:30500',
    [string]$TargetHost = '10.0.0.90',
    [string]$DeviceId = '001190520',
    [ValidateRange(20, 180)]
    [int]$ControllerSampleSeconds = 30,
    [ValidateRange(6, 120)]
    [int]$ObserverSampleSeconds = 9,
    [string]$EvidenceDir = '',
    [string]$MongoExe = 'D:\software\mongodb_3.6\mongodb\bin\mongo.exe'
)

$ErrorActionPreference = 'Stop'
$suffix = [guid]::NewGuid().ToString('N').Substring(0, 10)
$username = "rtc_app_$suffix"
$password = "T!$([guid]::NewGuid().ToString('N'))"
$uid = $null
$instanceId = $null
$accessToken = $null
$powershell = (Get-Process -Id $PID).Path

if ($ConsoleBase.StartsWith('https://') -and
    -not (Get-Command Invoke-RestMethod).Parameters.ContainsKey('SkipCertificateCheck')) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    [Net.ServicePointManager]::ServerCertificateValidationCallback = { $true }
}

function Invoke-JsonRequest([string]$Method, [string]$Uri, [object]$Body = $null,
                            [string]$Bearer = '') {
    $headers = @{}
    if ($Bearer) { $headers.Authorization = "Bearer $Bearer" }
    $request = @{
        Method = $Method
        Uri = $Uri
        Headers = $headers
        TimeoutSec = 30
    }
    if ($null -ne $Body) {
        $request.ContentType = 'application/json'
        $request.Body = $Body | ConvertTo-Json -Compress -Depth 12
    }
    if ($Uri.StartsWith('https://') -and
        (Get-Command Invoke-RestMethod).Parameters.ContainsKey('SkipCertificateCheck')) {
        $request.SkipCertificateCheck = $true
    }
    Invoke-RestMethod @request
}

function Assert-ConsoleSessionPersistence([string]$SubjectId) {
    if (-not (Test-Path -LiteralPath $MongoExe)) {
        throw "Mongo shell is required for the Console persistence gate: $MongoExe"
    }
    $query = @"
var u='$SubjectId';
var sessions=db.c_remote_session.find({subject_id:u}).toArray();
var sessionIds=sessions.map(function(v){return v.logical_session_id;});
var events=db.c_remote_session_event.find({logical_session_id:{`$in:sessionIds}}).toArray();
function count(t){return events.filter(function(v){return v.event_type===t;}).length;}
var rtcLocalIds={};
sessions.forEach(function(v){if((v.transports||[]).indexOf('rtc_local')>=0){rtcLocalIds[v.logical_session_id]=1;}});
events.forEach(function(v){
  if((v.transports||[]).indexOf('rtc_local')>=0||(v.previous_transports||[]).indexOf('rtc_local')>=0){
    rtcLocalIds[v.logical_session_id]=1;
  }
});
print('PX_SESSION_STATS:'+JSON.stringify({
  sessions:sessions.length,
  active:sessions.filter(function(v){return v.active===true;}).length,
  malformed:sessions.filter(function(v){return !v.device_id||!v.stream_id||!v.opened_timestamp||!v.updated_timestamp;}).length,
  rtcLocal:Object.keys(rtcLocalIds).length,
  opened:count('SessionOpened'),
  roleChanged:count('RoleChanged'),
  takeover:count('Takeover'),
  takeoverRelated:events.filter(function(v){return v.event_type==='Takeover'&&!!v.related_session_id;}).length,
  closed:count('SessionClosed'),
  current:sessions.map(function(v){return {id:v.logical_session_id,role:v.role,active:v.active,transports:v.transports};})
}));
"@
    # Browser process termination can take longer than the five-second ICE
    # reconnect grace to become observable on every Windows WebRTC path. Poll
    # the reliable Render -> Service -> Console snapshots to a bounded final
    # state instead of sampling an arbitrary instant in that transition.
    $deadline = (Get-Date).AddSeconds(50)
    $stats = $null
    do {
        Start-Sleep -Seconds 3
        $raw = (& $MongoExe db_gr_console_server --quiet --eval $query | Out-String)
        $line = @($raw -split "`r?`n" | Where-Object { $_ -like 'PX_SESSION_STATS:*' }) | Select-Object -Last 1
        if (-not $line) { throw "Console persistence query returned no result: $raw" }
        $stats = $line.Substring('PX_SESSION_STATS:'.Length) | ConvertFrom-Json
        if ($stats.sessions -ge 3 -and $stats.active -eq 0 -and $stats.closed -ge 3) { break }
    } while ((Get-Date) -lt $deadline)
    if ($stats.sessions -lt 3 -or $stats.active -ne 0 -or $stats.malformed -ne 0 -or
        $stats.rtcLocal -lt 3 -or $stats.opened -lt 3 -or $stats.roleChanged -lt 1 -or
        $stats.takeover -lt 1 -or $stats.takeoverRelated -lt 1 -or $stats.closed -lt 3) {
        throw "Console logical-session persistence gate failed: $($stats | ConvertTo-Json -Compress)"
    }
    Write-Host "APP_MULTI_PHASE: console-persistence PASS $($stats | ConvertTo-Json -Compress)"
}

try {
    $guest = Invoke-JsonRequest Post "$ConsoleBase/api/v1/session/guest" `
        @{ client_nonce = "app_guest_$suffix"; client_type = 'panel' }
    if ($guest.code -ne 200 -or -not $guest.data.access_token) { throw 'guest session failed' }

    $registered = Invoke-JsonRequest Post "$ConsoleBase/api/v1/user/register" `
        @{ username = $username; password = $password } $guest.data.access_token
    if ($registered.code -ne 200 -or -not $registered.data.uid) { throw 'registration failed' }
    $uid = [string]$registered.data.uid

    $login = Invoke-JsonRequest Post "$ConsoleBase/api/v1/session/user/login" `
        @{ username = $username; password = $password; client_type = 'panel' }
    if ($login.code -ne 200 -or -not $login.data.access_token) { throw 'user login failed' }
    $accessToken = [string]$login.data.access_token

    $started = Invoke-JsonRequest Post `
        "$ConsoleBase/api/v1/user/apps/$([Uri]::EscapeDataString($AppId))/start" `
        @{ client_nonce = "app_start_$suffix" } $accessToken
    if ($started.code -ne 200 -or -not $started.data.instance_id) { throw 'app start failed' }
    $instanceId = [string]$started.data.instance_id

    $deadline = (Get-Date).AddSeconds(45)
    $instance = $started.data
    while ($instance.state -notin @('running', 'failed', 'stopped') -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $instances = Invoke-JsonRequest Get "$ConsoleBase/api/v1/user/instances" $null $accessToken
        $instance = @($instances.data | Where-Object { $_.instance_id -eq $instanceId }) | Select-Object -First 1
        if (-not $instance) { throw "started instance disappeared: $instanceId" }
    }
    if ($instance.state -ne 'running') {
        throw "app instance did not enter running: id=$instanceId state=$($instance.state) error=$($instance.error_code)"
    }

    Write-Host "APP_MULTI_PHASE: instance-running app=$AppId instance=$instanceId port=$($instance.listen_port)"
    $multiArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        (Join-Path $PSScriptRoot 'run_rtc_multi_session_lan_case.ps1'),
        '-ConnectionMode', $ConnectionMode,
        '-ExpectedCandidate', $ExpectedCandidate,
        '-ConsoleBase', $ConsoleBase,
        '-TargetHost', $TargetHost,
        '-DeviceId', $DeviceId,
        '-InstanceId', $instanceId,
        '-BearerToken', $accessToken,
        '-ControllerSampleSeconds', [string]$ControllerSampleSeconds,
        '-ObserverSampleSeconds', [string]$ObserverSampleSeconds
    )
    if ($EvidenceDir) { $multiArgs += @('-EvidenceDir', $EvidenceDir) }
    & $powershell @multiArgs
    if ($LASTEXITCODE -ne 0) { throw "app multi-session gate exited with $LASTEXITCODE" }
    Assert-ConsoleSessionPersistence $uid
    Write-Host "RESULT: PASS app=$AppId instance=$instanceId"
}
finally {
    if ($instanceId -and $accessToken) {
        try {
            Invoke-JsonRequest Post `
                "$ConsoleBase/api/v1/user/instances/$([Uri]::EscapeDataString($instanceId))/stop" `
                @{ reason = 'acceptance_complete' } $accessToken | Out-Null
        } catch {
            Write-Warning "failed to stop test instance ${instanceId}: $($_.Exception.Message)"
        }
    }
    if ($uid -and $uid -match '^[A-Za-z0-9_-]+$' -and (Test-Path -LiteralPath $MongoExe)) {
        $cleanup = @"
var u='$uid';
var ids=db.c_app_instance.find({owner_type:'user',owner_id:u},{instance_id:1,_id:0}).toArray().map(function(v){return v.instance_id;});
var sessionIds=db.c_remote_session.distinct('logical_session_id',{subject_id:u});
var registration=db.c_event.findOne({action:'user_register',target_id:u,result:'success'});
if(registration){db.c_user_session.deleteMany({subject_id:registration.actor_id});}
db.c_connection_ticket.deleteMany({subject_id:u});
db.c_remote_session_event.deleteMany({logical_session_id:{`$in:sessionIds}});
db.c_remote_session.deleteMany({subject_id:u});
db.c_app_instance.deleteMany({owner_type:'user',owner_id:u});
db.c_user_session.deleteMany({subject_id:u});
db.c_user_group_member.deleteMany({uid:u});
db.c_user_device.deleteMany({uid:u});
db.c_user.deleteMany({uid:u});
db.c_event.deleteMany({`$or:[{actor_id:u},{target_id:u},{target_id:{`$in:ids}}]});
printjson({users:db.c_user.count({uid:u}),instances:db.c_app_instance.count({owner_id:u}),tickets:db.c_connection_ticket.count({subject_id:u})});
"@
        & $MongoExe db_gr_console_server --quiet --eval $cleanup
    }
}
