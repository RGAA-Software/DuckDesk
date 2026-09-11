param(
    [ValidateRange(1,20)][int]$Mbps=4,
    [ValidateRange(15,240)][int]$ObserveSeconds=180,
    [ValidateRange(1,180)][int]$MouseSweepSeconds=120,
    [ValidateSet(0,30,60,120)][int]$FramesPerSecond=0,
    [switch]$CapturePackets
)
$ErrorActionPreference='Stop'
$base='https://39.71.45.66:4600'
$appId='app-10-ac0adf25'
$credentials=Get-Content (Join-Path (Split-Path $PSScriptRoot -Parent) '.env/node90_license.json') -Raw|ConvertFrom-Json
$webSession=[Microsoft.PowerShell.Commands.WebRequestSession]::new()
$headers=@{Origin=$base}
function AdminApi([string]$Path,[object]$Body){
    $args=@{Uri="$base$Path";WebSession=$webSession;Headers=$headers;SkipCertificateCheck=$true;NoProxy=$true;TimeoutSec=20}
    if($null -ne $Body){$args.Method='Post';$args.ContentType='application/json';$args.Body=$Body|ConvertTo-Json -Depth 10 -Compress}
    $response=Invoke-RestMethod @args
    if($response.code -notin @(0,200)){throw "Admin API rejected request: $($response.code) $($response.message)"}
    return $response.data
}
$login=AdminApi '/api/v1/session/admin/login' @{username=$credentials.username;password=$credentials.password}
$headers['X-CSRF-Token']=$login.csrf_token
$original=(AdminApi '/api/v1/app/control/app/list' $null)|Where-Object app_id -eq $appId
if(-not $original){throw 'Acceptance app missing'}
$changed=$null
try {
    $candidate=$original|ConvertTo-Json -Depth 10|ConvertFrom-Json
    $candidate.encoder_bitrate=$Mbps
    if($FramesPerSecond){$candidate.encoder_fps=$FramesPerSecond}
    $changed=AdminApi '/api/v1/app/control/app/save' $candidate
    Write-Output "BITRATE_COMPARISON configured_mbps=$Mbps original_mbps=$($original.encoder_bitrate) fps=$($candidate.encoder_fps)"
    if($CapturePackets){
        & (Join-Path $PSScriptRoot 'test_udp_media_packet_capture.ps1') -Seconds $ObserveSeconds
    } else {
        & (Join-Path $PSScriptRoot 'test_udp_media_v2_windows.ps1') -ObserveSeconds $ObserveSeconds -MouseSweep -MouseSweepSeconds $MouseSweepSeconds `
            -ExpectedFramesPerSecond $candidate.encoder_fps
    }
} finally {
    if($changed){
        # Optimistic version check prevents undoing a concurrent administrator edit.
        $original.version=$changed.version
        [void](AdminApi '/api/v1/app/control/app/save' $original)
        Write-Output "BITRATE_RESTORED configured_mbps=$($original.encoder_bitrate) fps=$($original.encoder_fps)"
    }
}
