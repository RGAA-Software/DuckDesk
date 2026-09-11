#requires -Version 7.0
param(
    [string]$AppId = 'app-10-ac0adf25',
    [ValidateRange(15, 240)][int]$ObserveSeconds = 180,
    [switch]$MoveRight,
    [switch]$MouseSweep,
    [ValidateRange(1,180)][int]$MouseSweepSeconds = 120,
    [ValidateRange(15,120)][int]$ExpectedFramesPerSecond = 30,
    [ValidateRange(0.1,0.9)][double]$MouseSweepWidth = 0.85,
    [ValidateRange(0.1,0.8)][double]$MouseSweepHeight = 0.75
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$base = 'https://39.71.45.66:4600'
$clientPath = Join-Path $repo 'build_official/dist/px_client.exe'
$sourcePath = Join-Path $repo 'build_official/src/px_client/px_client.exe'
if ((Get-FileHash $clientPath).Hash -ne (Get-FileHash $sourcePath).Hash) { throw 'Client has not been published to dist.' }
$output = Join-Path $repo ('test-results/udp-media-v2/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
if (-not ('PixelsMediaAcceptance' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PixelsMediaAcceptance {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@
}
[void][PixelsMediaAcceptance]::SetProcessDpiAwarenessContext([IntPtr](-4))
function Api([string]$Path, [object]$Body, [string]$Token = '') {
    $headers = @{Origin=$base}
    if ($Token) { $headers.Authorization="Bearer $Token" }
    $args = @{Uri="$base$Path";Headers=$headers;SkipCertificateCheck=$true;NoProxy=$true;TimeoutSec=20}
    if ($null -ne $Body) { $args.Method='Post';$args.ContentType='application/json';$args.Body=$Body|ConvertTo-Json -Compress -Depth 8 }
    $result = Invoke-RestMethod @args
    if ($result.code -and $result.code -ne 200) { throw "API failed: $Path code=$($result.code)" }
    return $result.data
}
function Capture([Diagnostics.Process]$Process, [string]$Name) {
    $Process.Refresh()
    $window = $Process.MainWindowHandle
    if ($window -eq [IntPtr]::Zero) { return $false }
    [void][PixelsMediaAcceptance]::ShowWindow($window, 9)
    [void][PixelsMediaAcceptance]::SetForegroundWindow($window)
    if ([PixelsMediaAcceptance]::GetForegroundWindow() -ne $window) {
        $activation = New-Object -ComObject WScript.Shell
        try { [void]$activation.AppActivate($Process.Id) }
        finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($activation) }
    }
    Start-Sleep -Milliseconds 500
    if ([PixelsMediaAcceptance]::GetForegroundWindow() -ne $window) {
        Write-Warning "Screenshot skipped: Client is not foreground ($Name)."
        return $false
    }
    $rect = [PixelsMediaAcceptance+Rect]::new()
    if (-not [PixelsMediaAcceptance]::GetWindowRect($window,[ref]$rect)) { return $false }
    if ($rect.Right -le $rect.Left -or $rect.Bottom -le $rect.Top) { return $false }
    $bitmap = [Drawing.Bitmap]::new($rect.Right-$rect.Left,$rect.Bottom-$rect.Top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left,$rect.Top,0,0,$bitmap.Size)
        if ([PixelsMediaAcceptance]::GetForegroundWindow() -ne $window) { return $false }
        $bitmap.Save((Join-Path $output "$Name.png"))
    }
    finally { $graphics.Dispose();$bitmap.Dispose() }
    return $true
}
$client=$null
$instance=$null
$token=''
$mouseCompleted=$false
$logPath='C:/Users/Public/Pixels/px_logs/app.39.71.45.66.log'
$initialLines=if(Test-Path $logPath){@(Get-Content $logPath).Count}else{0}
try {
    $credentials=Get-Content (Join-Path $repo '.env/node90_test_user.json') -Raw|ConvertFrom-Json
    $login=Api '/api/v1/session/user/login' @{username=$credentials.username;password=$credentials.password;client_type='panel'}
    $token=$login.access_token
    $nonce=[guid]::NewGuid().ToString('N')
    $instance=Api "/api/v1/user/apps/$AppId/start" @{client_nonce=$nonce} $token
    if ($instance.state -ne 'running') { throw "Instance not running: $($instance.state)" }
    $ticket=Api "/api/v1/user/instances/$($instance.instance_id)/ticket" @{client_nonce=$nonce;join_mode='control'} $token
    $route=[uri]$ticket.launch_url
    $start=[Diagnostics.ProcessStartInfo]::new($clientPath)
    $start.WorkingDirectory=Split-Path $clientPath -Parent
    $start.UseShellExecute=$false
    # This is the actual interactive Client under visual acceptance, not a background helper.
    $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Normal
    $args=@("--host=$($route.Host)","--port=$($route.Port)",'--console_host=39.71.45.66','--console_port=4600','--console_ssl=true',
        '--audio=1','--clipboard=1',"--stream_id=$($ticket.stream_id)",'--conn_type=console_ticket',
        "--device_id=udp_v2_$nonce",'--remote_device_id=371289832','--only_viewing=0','--max_num_of_screen=1',
        "--connection_instance_id=$($instance.instance_id)",
        "--connection_ticket=$([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($ticket.ticket)))","--connection_nonce=$nonce")
    foreach($argument in $args){$start.ArgumentList.Add($argument)}
    $client=[Diagnostics.Process]::Start($start)
    Write-Output "CLIENT_STARTED pid=$($client.Id) instance=$($instance.instance_id) route=$($route.Host):$($route.Port) output=$output"
    $deadline=(Get-Date).AddSeconds($ObserveSeconds)
    $captured=$false
    while((Get-Date) -lt $deadline -and -not $client.HasExited){
        Start-Sleep -Milliseconds 500
        if(-not $captured -and (Get-Date) -gt $deadline.AddSeconds(-$ObserveSeconds+10)){
            $captured=Capture $client 'before-input'
            if($MouseSweep -and $captured){
                $rect=[PixelsMediaAcceptance+Rect]::new()
                if([PixelsMediaAcceptance]::GetWindowRect($client.MainWindowHandle,[ref]$rect)){
                    $sweepBegin="MOUSE_SWEEP_BEGIN $(Get-Date -Format o) requested_seconds=$MouseSweepSeconds width=$MouseSweepWidth height=$MouseSweepHeight"
                    Write-Output $sweepBegin
                    $sweepBegin | Set-Content -LiteralPath (Join-Path $output 'input-evidence.log') -Encoding utf8
                    $sweep=[Diagnostics.Stopwatch]::StartNew()
                    $moves=0
                    $nextSnapshot=30
                    while($sweep.Elapsed.TotalSeconds -lt $MouseSweepSeconds -and (Get-Date) -lt $deadline -and -not $client.HasExited -and
                          [PixelsMediaAcceptance]::GetForegroundWindow() -eq $client.MainWindowHandle){
                        if(-not [PixelsMediaAcceptance]::GetWindowRect($client.MainWindowHandle,[ref]$rect)){break}
                        $phase=$sweep.Elapsed.TotalSeconds*18
                        $x=($rect.Left+$rect.Right)/2+[Math]::Sin($phase)*($rect.Right-$rect.Left)*$MouseSweepWidth/2
                        $y=($rect.Top+$rect.Bottom)/2+[Math]::Cos($phase*0.7)*($rect.Bottom-$rect.Top)*$MouseSweepHeight/2
                        [void][PixelsMediaAcceptance]::SetCursorPos([int]$x,[int]$y)
                        ++$moves
                        if($sweep.Elapsed.TotalSeconds -ge $nextSnapshot){
                            [void](Capture $client "mouse-$nextSnapshot")
                            $nextSnapshot+=30
                        }
                        Start-Sleep -Milliseconds 8
                    }
                    $sweepEnd="MOUSE_SWEEP_END $(Get-Date -Format o) actual_seconds=$([Math]::Round($sweep.Elapsed.TotalSeconds,2)) moves=$moves"
                    Write-Output $sweepEnd
                    $sweepEnd | Add-Content -LiteralPath (Join-Path $output 'input-evidence.log') -Encoding utf8
                    $mouseCompleted=$sweep.Elapsed.TotalSeconds -ge $MouseSweepSeconds*0.95 -and $moves -ge $MouseSweepSeconds*30
                    [void](Capture $client 'after-mouse')
                }
            }
            if($MoveRight -and $captured -and [PixelsMediaAcceptance]::GetForegroundWindow() -eq $client.MainWindowHandle){
                try {[PixelsMediaAcceptance]::keybd_event(0x27,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 1800}
                finally {[PixelsMediaAcceptance]::keybd_event(0x27,0,2,[UIntPtr]::Zero)}
                [void](Capture $client 'after-input')
            }
        }
    }
    $finalScreenshot=$false
    if(-not $client.HasExited){$finalScreenshot=Capture $client 'final'}
    $evidence=(@(Get-Content $logPath)|Select-Object -Skip $initialLines)-join "`n"
    $selected=$evidence -split "`n"|Where-Object {$_ -match 'UDP media v2|udp recv pkt|frame.*decoded|Decoder.*error|decode.*fail|watchdog|Video size changed|present.*frame|LAT-decode|LAT-net|Video frame came'}
    $selected | Set-Content -LiteralPath (Join-Path $output 'media-evidence.log') -Encoding utf8
    $windows=@($selected | ForEach-Object {
        if($_ -match 'window: frames=(\d+), fps=([\d.]+), max_gap_ms=([\d.]+), gaps_gt_100ms=(\d+)'){
            [pscustomobject]@{fps=[double]$Matches[2];gap=[double]$Matches[3];stalls=[int]$Matches[4]}
        }
    })
    $summary=[ordered]@{
        expected_fps=$ExpectedFramesPerSecond
        window_count=$windows.Count
        average_fps=($windows|Measure-Object fps -Average).Average
        minimum_fps=($windows|Measure-Object fps -Minimum).Minimum
        max_delivery_gap_ms=($windows|Measure-Object gap -Maximum).Maximum
        gaps_over_100ms=($windows|Measure-Object stalls -Sum).Sum
        mouse_required=[bool]$MouseSweep
        mouse_completed=$mouseCompleted
        # This is receive-window evidence, not a claim that every displayed frame is correct.
        receive_window_pass=$false
        visual_acceptance='requires_review'
        final_screenshot_valid=$finalScreenshot
    }
    $summary.receive_window_pass=$windows.Count -ge 5 -and $summary.minimum_fps -ge $ExpectedFramesPerSecond*0.95 -and
        $summary.max_delivery_gap_ms -lt 100 -and (-not $MouseSweep -or $mouseCompleted) -and -not $client.HasExited
    $summary|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
    $selected|Where-Object {$_ -match 'UDP media v2 window|Decoder.*error|decode.*fail'}|ForEach-Object{Write-Output $_}
    Write-Output "RECEIVE_WINDOW_PASS=$($summary.receive_window_pass) mouse_completed=$mouseCompleted summary=$output/summary.json"
    Write-Output "RESULT exited=$($client.HasExited) screenshot=$captured v2_video=$($evidence -match 'UDP media v2 video delivered') v2_audio=$($evidence -match 'UDP media v2 audio delivered')"
} finally {
    if($client -and -not $client.HasExited){$client.Kill();$client.WaitForExit(5000)|Out-Null}
    if($instance -and $token){
        try {[void](Api "/api/v1/user/instances/$($instance.instance_id)/stop" @{reason='bounded_udp_media_v2_acceptance'} $token)}
        catch {Write-Warning 'Acceptance instance stop request failed; inspect its normal disconnect grace state.'}
    }
}
