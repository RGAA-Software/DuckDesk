param(
    [ValidateRange(1, 1000)]
    [int]$Rounds = 100,
    [ValidateRange(6, 600)]
    [int]$SampleSeconds = 9,
    [string]$ConsoleBase = 'https://127.0.0.1:30500',
    [string]$TargetHost = '10.0.0.90',
    [string]$DeviceId = '001190520',
    [string]$MongoExe = 'D:\software\mongodb_3.6\mongodb\bin\mongo.exe'
)

$ErrorActionPreference = 'Stop'
$suffix = [guid]::NewGuid().ToString('N').Substring(0, 10)
$username = "rtc_stability_$suffix"
$password = "T!$([guid]::NewGuid().ToString('N'))"
$uid = $null
$pass = 0
$fail = 0
$failRounds = [Collections.Generic.List[int]]::new()
$started = Get-Date

if ($ConsoleBase.StartsWith('https://') -and
    -not (Get-Command Invoke-RestMethod).Parameters.ContainsKey('SkipCertificateCheck')) {
    # Windows PowerShell 5.1 has no per-request switch. This callback is scoped
    # to this short-lived acceptance-test process and permits the bundled cert.
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
    }
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        try {
            return Invoke-RestMethod @request
        }
        catch {
            $status = [int]$_.Exception.Response.StatusCode
            if ($status -ne 429 -or $attempt -eq $Attempts) { throw }
            Write-Host "RATE_LIMIT_WAIT attempt=$attempt/$Attempts"
            Start-Sleep -Seconds 15
        }
    }
}

try {
    $guest = Invoke-JsonPost "$ConsoleBase/api/v1/session/guest" `
        @{ client_nonce = "stability_guest_$suffix"; client_type = 'panel' } '' 9
    if ($guest.code -ne 200 -or -not $guest.data.access_token) { throw 'guest session failed' }

    $registered = Invoke-JsonPost "$ConsoleBase/api/v1/user/register" `
        @{ username = $username; password = $password } $guest.data.access_token 9
    if ($registered.code -ne 200 -or -not $registered.data.uid) { throw 'registration failed' }
    $uid = $registered.data.uid

    $login = Invoke-JsonPost "$ConsoleBase/api/v1/session/user/login" `
        @{ username = $username; password = $password; client_type = 'panel' } '' 9
    if ($login.code -ne 200 -or -not $login.data.access_token) { throw 'user login failed' }
    $token = $login.data.access_token

    for ($round = 1; $round -le $Rounds; $round++) {
        $roundStart = Get-Date
        try {
            # A standard RTC session may legitimately select host, srflx or
            # relay. Candidate-specific guarantees are covered by the
            # dedicated host/TURN UDP/TURN TCP cases; stability only gates
            # end-to-end connectivity, media progress and resource cleanup.
            & (Join-Path $PSScriptRoot 'run_rtc_lan_case.ps1') `
                -ExpectedCandidate any -SampleSeconds $SampleSeconds -Quiet `
                -ConsoleBase $ConsoleBase -TargetHost $TargetHost -DeviceId $DeviceId `
                -BearerToken $token -MongoExe $MongoExe
            $pass++
            Write-Host ("ROUND {0:D3}/{1} PASS elapsed={2:N1}s" -f $round, $Rounds, ((Get-Date) - $roundStart).TotalSeconds)
        }
        catch {
            $fail++
            $failRounds.Add($round)
            Write-Host ("ROUND {0:D3}/{1} FAIL elapsed={2:N1}s reason={3}" -f $round, $Rounds, ((Get-Date) - $roundStart).TotalSeconds, $_.Exception.Message)
        }
        if ($round % 10 -eq 0) {
            try {
                $cdpChrome = @(Get-CimInstance Win32_Process -Filter "Name='chrome.exe'" -ErrorAction Stop |
                    Where-Object { $_.CommandLine -like '*cdp-diag-*' }).Count
            }
            catch {
                $cdpChrome = 'unavailable'
            }
            Write-Host "RESOURCE_CHECK round=$round cdpChrome=$cdpChrome"
        }
    }
}
finally {
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

Write-Host ("STABILITY_SUMMARY pass={0} fail={1} failRounds={2} totalMinutes={3:N1}" -f `
    $pass, $fail, ($failRounds -join ','), ((Get-Date) - $started).TotalMinutes)
if ($fail -gt 0) { exit 1 }
