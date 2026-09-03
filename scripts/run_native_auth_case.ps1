param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('account', 'guest')]
    [string]$Mode,
    [string]$ConsoleBase = 'https://localhost:30500',
    [string]$TargetHost = '10.0.0.90',
    [int]$TargetPort = 20371,
    [string]$DeviceId = '001190520',
    [ValidateSet('webrtc_direct', 'webrtc', 'websocket', 'udp_direct')]
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
    [ValidateRange(0, 900)]
    [int]$HoldSeconds = 0,
    [switch]$OmitRemoteDeviceId,
    [switch]$ExpectOccupied,
    [switch]$BlockTargetUdp,
    [switch]$RtcRestartAcceptance,
    [ValidateRange(0, 65535)]
    [int]$PanelProbePort = 0,
    [string]$MongoExe = 'D:\software\mongodb_3.6\mongodb\bin\mongo.exe'
)

$ErrorActionPreference = 'Stop'

function Add-ProcessArgument([Diagnostics.ProcessStartInfo]$StartInfo, [string]$Value) {
    # ArgumentList is available on PowerShell 7/.NET, but Windows PowerShell
    # exposes no usable collection here. Keep the acceptance script runnable
    # from both hosts because the launcher itself uses Windows PowerShell.
    if ($null -ne $StartInfo.PSObject.Properties['ArgumentList']) {
        [void]$StartInfo.ArgumentList.Add($Value)
        return
    }
    $escaped = '"' + (($Value -replace '(\\*)"', '$1$1\"') -replace '(\\*)$', '$1$1') + '"'
    if ($StartInfo.Arguments) {
        $StartInfo.Arguments += ' '
    }
    $StartInfo.Arguments += $escaped
}

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

function Invoke-JsonPost([string]$Uri, [object]$Body, [string]$Bearer = '', [int]$Attempts = 6) {
    $jsonBody = $Body | ConvertTo-Json -Compress -Depth 12
    $authHeaderPath = $null
    if ($Bearer) {
        $authHeaderPath = Join-Path $env:TEMP "px_auth_$PID$([guid]::NewGuid().ToString('N')).hdr"
        [IO.File]::WriteAllText(
            $authHeaderPath,
            "Authorization: Bearer $Bearer",
            [Text.UTF8Encoding]::new($false))
    }
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        $status = 0
        try {
            $startInfo = [Diagnostics.ProcessStartInfo]::new()
            $startInfo.FileName = 'curl.exe'
            $startInfo.UseShellExecute = $false
            $startInfo.RedirectStandardInput = $true
            $startInfo.RedirectStandardOutput = $true
            $startInfo.RedirectStandardError = $true
            $curlArgs = @(
                '--insecure', '--ssl-auto-client-cert', '--silent', '--show-error', '--max-time', '30',
                '--user-agent', 'GammaRay-Native-Acceptance',
                '--request', 'POST', '--header', 'Content-Type: application/json',
                '--data-binary', '@-', '--write-out', "`n%{http_code}"
            )
            if ($authHeaderPath) {
                $curlArgs += '--header', "@$authHeaderPath"
            }
            $curlArgs += $Uri
            foreach ($value in $curlArgs) {
                Add-ProcessArgument $startInfo $value
            }
            $curl = [Diagnostics.Process]::new()
            $curl.StartInfo = $startInfo
            [void]$curl.Start()
            $curl.StandardInput.Write($jsonBody)
            $curl.StandardInput.Close()
            $stdout = $curl.StandardOutput.ReadToEnd()
            $stderr = $curl.StandardError.ReadToEnd()
            $curl.WaitForExit()
            $curlExitCode = $curl.ExitCode
            $curl.Dispose()
            $lines = @($stdout -split "\r?\n")
            $status = if ($lines.Count -gt 0 -and $lines[-1] -match '^\d{3}$') {
                [int]$lines[-1]
            } else { 0 }
            $responseBody = if ($lines.Count -gt 1) {
                ($lines[0..($lines.Count - 2)] -join "`n").Trim()
            } else { '' }
            if ($curlExitCode -ne 0 -or $status -lt 200 -or $status -ge 300) {
                throw "curl_exit=$curlExitCode http_status=$status detail=$stderr body=$responseBody"
            }
            if ($authHeaderPath) {
                Remove-Item -LiteralPath $authHeaderPath -Force -ErrorAction SilentlyContinue
            }
            return $responseBody | ConvertFrom-Json
        }
        catch {
            $retryable = $status -eq 429 -or $status -eq 0
            if (-not $retryable -or $attempt -eq $Attempts) {
                if ($authHeaderPath) {
                    Remove-Item -LiteralPath $authHeaderPath -Force -ErrorAction SilentlyContinue
                }
                throw "HTTP request failed: uri=$Uri status=$status reason=$($_.Exception.Message)"
            }
            $waitSeconds = if ($status -eq 429) { 10 } else { 1 }
            Write-Host "HTTP_RETRY status=$status attempt=$attempt/$Attempts wait_seconds=$waitSeconds"
            Start-Sleep -Seconds $waitSeconds
        }
    }
    if ($authHeaderPath) {
        Remove-Item -LiteralPath $authHeaderPath -Force -ErrorAction SilentlyContinue
    }
}

function ConvertTo-Base64([string]$Value) {
    [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Value))
}

function ConvertTo-Sha256([string]$Value) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = $algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($Value))
        return ([BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

# Schannel can reject the first non-interactive request when the local
# self-signed Console asks for TLS renegotiation. Warm the TLS connection with
# a read-only page request; do not consume a rate-limited guest session.
$tlsReady = $false
foreach ($warmupAttempt in 1..3) {
    $warmupInfo = [Diagnostics.ProcessStartInfo]::new()
    $warmupInfo.FileName = 'curl.exe'
    $warmupInfo.UseShellExecute = $false
    $warmupInfo.CreateNoWindow = $true
    $warmupInfo.RedirectStandardError = $true
    foreach ($value in @(
        '--insecure', '--ssl-auto-client-cert', '--silent', '--show-error', '--max-time', '10',
        '--user-agent', 'GammaRay-Native-Acceptance', '--output', 'NUL', "$ConsoleBase/"
    )) {
        Add-ProcessArgument $warmupInfo $value
    }
    $warmupProcess = [Diagnostics.Process]::new()
    $warmupProcess.StartInfo = $warmupInfo
    [void]$warmupProcess.Start()
    $warmupError = $warmupProcess.StandardError.ReadToEnd()
    $warmupProcess.WaitForExit()
    $warmupExitCode = $warmupProcess.ExitCode
    $warmupProcess.Dispose()
    if ($warmupExitCode -eq 0) {
        $tlsReady = $true
        break
    }
    Write-Host "TLS_WARMUP_RETRY attempt=$warmupAttempt exit_code=$warmupExitCode detail=$warmupError"
    Start-Sleep -Milliseconds 250
}
if (-not $tlsReady) {
    throw "Console TLS warm-up failed: $ConsoleBase"
}

$suffix = [guid]::NewGuid().ToString('N').Substring(0, 10)
$streamId = "native_${Mode}_$suffix"
$clientId = "acceptance_$suffix"
$uid = $null
$guestTokenHash = $null
$process = $null
$probeProcess = $null
$probeConfigPath = $null
$probeStdoutPath = $null
$probeStderrPath = $null
$rtcIceEnvironmentSet = $false
$firewallRule = $null
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
    if ($Mode -eq 'guest' -and $NetworkType -ne 'webrtc_direct') {
        throw 'password-direct native acceptance must use webrtc_direct'
    }
    if ($OmitRemoteDeviceId -and $Mode -ne 'guest') {
        throw '-OmitRemoteDeviceId is only valid for password-direct guest acceptance'
    }
    if ($ExpectOccupied -and ($Mode -ne 'account' -or $NetworkType -ne 'websocket')) {
        throw '-ExpectOccupied requires account mode with -NetworkType websocket'
    }
    if ($BlockTargetUdp -and $NetworkType -ne 'udp_direct') {
        throw '-BlockTargetUdp requires -NetworkType udp_direct'
    }
    $nonce = "native_$suffix"
    $connectionTicket = ''
    $initialRtcIceConfig = ''
    $restartRevision = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $relayHost = ([uri]$ConsoleBase).Host
    $relayPort = 30502

    if ($Mode -eq 'account') {
        $username = "native_$suffix"
        $password = "T!$([guid]::NewGuid().ToString('N'))"
        $guest = Invoke-JsonPost "$ConsoleBase/api/v1/session/guest" `
            @{ client_nonce = "guest_$suffix"; client_type = 'panel' }
        $guestTokenHash = ConvertTo-Sha256 $guest.data.access_token
        $registered = Invoke-JsonPost "$ConsoleBase/api/v1/user/register" `
            @{ username = $username; password = $password } $guest.data.access_token
        $uid = $registered.data.uid
        $login = Invoke-JsonPost "$ConsoleBase/api/v1/session/user/login" `
            @{ username = $username; password = $password; client_type = 'panel' }
        $issued = Invoke-JsonPost "$ConsoleBase/api/v1/user/devices/$DeviceId/ticket" `
            @{ client_nonce = $nonce; join_mode = 'control' } `
            $login.data.access_token
        $connectionTicket = $issued.data.ticket
        $streamId = [string]$issued.data.stream_id
        $relayHost = $issued.data.relay_host
        $relayPort = [int]$issued.data.relay_port
        $initialRtcIceConfig = $issued.data.rtc_ice_config | ConvertTo-Json -Compress -Depth 12
        if (-not $connectionTicket -or -not $streamId) {
            throw 'account ticket issue returned an empty ticket or runtime stream ID'
        }

        if ($RtcRestartAcceptance) {
            if ($NetworkType -ne 'webrtc') {
                throw 'RTC restart acceptance requires -NetworkType webrtc'
            }
            $renewalToken = [string]$issued.data.renewal_token
            if (-not $renewalToken) {
                throw 'account ticket issue returned no renewal capability for RTC restart acceptance'
            }
            if ($PanelProbePort -eq 0) {
                $PanelProbePort = Get-Random -Minimum 32000 -Maximum 39000
            }
            $probeConfigPath = Join-Path $env:TEMP "px_rtc_restart_$suffix.json"
            $probeStdoutPath = Join-Path $env:TEMP "px_rtc_restart_$suffix.out.log"
            $probeStderrPath = Join-Path $env:TEMP "px_rtc_restart_$suffix.err.log"
            @{
                port = $PanelProbePort
                stream_id = $streamId
                console_base = $ConsoleBase
                renewal_token = $renewalToken
                client_nonce = $nonce
                instance_id = [string]$issued.data.instance_id
                revision = $restartRevision
                send_delay_ms = 7000
                guard_delay_ms = 2500
            } | ConvertTo-Json -Compress -Depth 12 | Set-Content -LiteralPath $probeConfigPath -Encoding utf8
            $probeScript = Join-Path $repoRoot 'scripts\rtc_restart_panel_probe.mjs'
            $probeProcess = Start-Process -FilePath 'node' -ArgumentList @($probeScript, "--config=$probeConfigPath") `
                -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput $probeStdoutPath -RedirectStandardError $probeStderrPath
            $probeDeadline = (Get-Date).AddSeconds(10)
            do {
                Start-Sleep -Milliseconds 100
                if ($probeProcess.HasExited) {
                    $probeError = if (Test-Path -LiteralPath $probeStderrPath) {
                        Get-Content -LiteralPath $probeStderrPath -Raw
                    } else { 'no stderr' }
                    throw "RTC restart Panel probe exited early: $probeError"
                }
                $probeOutput = if (Test-Path -LiteralPath $probeStdoutPath) {
                    Get-Content -LiteralPath $probeStdoutPath -Raw
                } else { '' }
            } while ($probeOutput -notmatch 'PANEL_PROBE_READY' -and (Get-Date) -lt $probeDeadline)
            if ($probeOutput -notmatch 'PANEL_PROBE_READY') {
                throw 'RTC restart Panel probe did not become ready'
            }
        }
    } elseif (-not $RemotePassword) {
        throw 'guest mode requires -RemotePassword or PX_TEST_REMOTE_PASSWORD'
    }

    $clientRemoteDeviceId = if ($OmitRemoteDeviceId) { '' } else { $DeviceId }
    $arguments = @(
        "--host=$TargetHost", "--port=$TargetPort",
        "--console_host=$(([uri]$ConsoleBase).Host)", "--console_port=$(([uri]$ConsoleBase).Port)",
        '--console_ssl=true', '--audio=1', '--clipboard=1',
        "--stream_id=$streamId", "--conn_type=$(if ($Mode -eq 'account') {'console_ticket'} else {'direct'})",
        "--network_type=$NetworkType", "--device_id=$clientId",
        "--remote_device_id=$clientRemoteDeviceId", '--enable_p2p=1', '--only_viewing=0',
        "--split_windows=$($SplitWindows.IsPresent.ToString().ToLowerInvariant())",
        "--max_num_of_screen=$MaxScreens",
        "--force_direct=$(if ($NetworkType -eq 'webrtc_direct') {1} else {0})",
        "--relay_host=$relayHost", "--relay_port=$relayPort"
    )
    if ($RtcRestartAcceptance) {
        $arguments += "--panel_server_port=$PanelProbePort"
    }
    if ($Mode -eq 'account') {
        $arguments += "--connection_ticket=$(ConvertTo-Base64 $connectionTicket)"
        $arguments += "--connection_nonce=$nonce"
    } else {
        $arguments += "--remote_device_rp=$(ConvertTo-Base64 $RemotePassword)"
    }

    if ($initialRtcIceConfig -and $initialRtcIceConfig -ne 'null') {
        [Environment]::SetEnvironmentVariable('PX_RTC_ICE_CONFIG', $initialRtcIceConfig, 'Process')
        $rtcIceEnvironmentSet = $true
    }
    if ($BlockTargetUdp) {
        $firewallRule = "PixelsNativeAcceptance_$PID`_$suffix"
        New-NetFirewallRule -Name $firewallRule -DisplayName $firewallRule `
            -Direction Outbound -Action Block -Protocol UDP `
            -RemoteAddress $TargetHost -RemotePort $TargetPort | Out-Null
    }
    $process = Start-Process -FilePath $ClientExe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $ClientExe -Parent) -WindowStyle Hidden -PassThru
    if ($rtcIceEnvironmentSet) {
        [Environment]::SetEnvironmentVariable('PX_RTC_ICE_CONFIG', $null, 'Process')
        $rtcIceEnvironmentSet = $false
    }

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
            if ($ExpectOccupied -and
                $evidence -match 'WebSocket session rejected; automatic reconnect disabled, reason=2') {
                break
            }
            $transportReady = switch ($NetworkType) {
                'webrtc_direct' {
                    $evidence -match 'Rtc local, connected\.' -and
                    $evidence -match 'first key frame'
                }
                'webrtc' {
                    $evidence -match 'Full WebRTC transport is ready' -and
                    $evidence -match 'RtcVideoSink OnFrame #1'
                }
                'udp_direct' {
                    if ($BlockTargetUdp) {
                        $evidence -match 'Udp direct media unavailable; reconnecting the media channel as direct WebSocket\.'
                    } else {
                        $evidence -match 'Udp direct first media received; keep UDP media transport\.'
                    }
                }
                default { $evidence -match 'Step: MsgNetworkConnected' }
            }
            $renderViewsReady = $ExpectedMonitorCount -le 1 -or
                $evidence -match "Render view pool expanded on demand: requested=$ExpectedMonitorCount, active_capacity=$ExpectedMonitorCount"
            $keyFramePattern = switch ($NetworkType) {
                'webrtc_direct' { 'first key frame' }
                'webrtc' { 'RtcVideoSink OnFrame #1' }
                default { 'First decoded video frame reached UI renderer' }
            }
            $keyFrameCount = ([regex]::Matches($evidence, $keyFramePattern)).Count
            # Direct RTC negotiates one stable track slot per monitor. Standard
            # RTC intentionally carries the selected monitor on one video track
            # and changes that track when the user switches monitors.
            $expectedKeyFrameCount = if ($NetworkType -eq 'webrtc_direct') {
                $ExpectedMonitorCount
            } else {
                1
            }
            $restartReady = -not $RtcRestartAcceptance -or (
                $evidence -match "Managed RTC ICE restart completed, generation=.+revision=$restartRevision" -and
                ([regex]::Matches($evidence, 'Ignore duplicate/stale RTC ICE configuration, revision=')).Count -ge 2
            )
            # RTC always supplies an audio track and initializes the player as
            # part of transport setup. WS/UDP initialize it only after the
            # remote loopback produces a PCM frame, which is not guaranteed on
            # an otherwise idle acceptance target.
            $audioReady = $NetworkType -notin @('webrtc_direct', 'webrtc') -or
                $evidence -match 'Init audio player'
            if ($transportReady -and $renderViewsReady -and
                $keyFrameCount -ge $expectedKeyFrameCount -and
                $evidence -match 'First decoded video frame reached UI renderer' -and
                $evidence -match 'File-transfer transport connected' -and
                $audioReady -and $restartReady) {
                break
            }
        }
    } while ((Get-Date) -lt $deadline)

    if ($ExpectOccupied) {
        $rejectionCount = ([regex]::Matches(
            $evidence,
            'WebSocket session rejected; automatic reconnect disabled, reason=2')).Count
        if ($rejectionCount -ne 1) {
            throw "expected exactly one occupied-session rejection, actual=$rejectionCount"
        }
        if ($evidence -match 'Step: MsgNetworkConnected') {
            throw 'occupied WebSocket was reported as application-connected before admission'
        }
        Start-Sleep -Seconds 3
        $latestEvidence = (@(Get-Content -LiteralPath $logPath) |
            Select-Object -Skip $initialLines) -join "`n"
        $latestRejectionCount = ([regex]::Matches(
            $latestEvidence,
            'WebSocket session rejected; automatic reconnect disabled, reason=2')).Count
        if ($latestRejectionCount -ne 1) {
            throw "occupied WebSocket retried after terminal rejection, count=$latestRejectionCount"
        }
        Write-Host "NATIVE_AUTH_CASE PASS mode=$Mode route=$NetworkType target=$DeviceId rejection=occupied retries=0"
        $exitCode = 0
        return
    }
    if ($process.HasExited) { throw "native client exited before acceptance, code=$($process.ExitCode)" }
    if ($evidence -match '(?i)authentication required|resource not found|ticket.+failed|connect failed') {
        throw 'native client log contains an authentication or connection failure'
    }
    switch ($NetworkType) {
        'webrtc_direct' {
            if ($evidence -notmatch 'Rtc local, connected\.') { throw 'native Direct RTC did not connect' }
            if ($evidence -notmatch 'first key frame') { throw 'native Direct RTC did not decode the first key frame' }
        }
        'webrtc' {
            if ($evidence -notmatch 'Full WebRTC transport is ready') { throw 'native standard RTC did not connect' }
            if ($evidence -notmatch 'RtcVideoSink OnFrame #1') { throw 'native standard RTC sink did not receive the first frame' }
        }
        'udp_direct' {
            if ($BlockTargetUdp -and $evidence -notmatch 'Udp direct media unavailable; reconnecting the media channel as direct WebSocket\.') {
                throw 'native UDP direct did not fall back to authenticated WebSocket media'
            }
            if (-not $BlockTargetUdp -and $evidence -notmatch 'Udp direct first media received; keep UDP media transport\.') {
                throw 'native UDP direct did not receive media over UDP'
            }
        }
        'websocket' {
            if ($evidence -notmatch 'Step: MsgNetworkConnected') { throw 'native WebSocket transport did not connect' }
        }
    }
    if ($evidence -notmatch 'First decoded video frame reached UI renderer') {
        throw 'native client decoded video but did not deliver it to the UI renderer'
    }
    if ($ExpectedMonitorCount -gt 1 -and
        $evidence -notmatch "Render view pool expanded on demand: requested=$ExpectedMonitorCount, active_capacity=$ExpectedMonitorCount") {
        throw "native render view pool did not expand to $ExpectedMonitorCount monitors"
    }
    $expectedKeyFrameCount = if ($NetworkType -eq 'webrtc_direct') {
        $ExpectedMonitorCount
    } else {
        1
    }
    $keyFramePattern = switch ($NetworkType) {
        'webrtc_direct' { 'first key frame' }
        'webrtc' { 'RtcVideoSink OnFrame #1' }
        default { 'First decoded video frame reached UI renderer' }
    }
    if (([regex]::Matches($evidence, $keyFramePattern)).Count -lt $expectedKeyFrameCount) {
        throw "native RTC received fewer than $expectedKeyFrameCount expected video-track first frames"
    }
    if ($evidence -notmatch 'File-transfer transport connected') { throw 'native file transport did not connect' }
    if ($NetworkType -in @('webrtc_direct', 'webrtc') -and
        $evidence -notmatch 'Init audio player') {
        throw 'native audio player did not initialize'
    }
    if ($RtcRestartAcceptance) {
        if ($evidence -match 'Managed RTC ICE restart failed') {
            throw 'managed RTC ICE restart reached a failed terminal state'
        }
        if ($evidence -notmatch "Panel requested RTC ICE restart, revision=$restartRevision") {
            $probeOutput = if (Test-Path -LiteralPath $probeStdoutPath) {
                (Get-Content -LiteralPath $probeStdoutPath -Raw).Trim()
            } else { 'probe stdout unavailable' }
            $probeError = if (Test-Path -LiteralPath $probeStderrPath) {
                (Get-Content -LiteralPath $probeStderrPath -Raw).Trim()
            } else { 'probe stderr unavailable' }
            throw "native client did not receive the Panel RTC restart command; probe=[$probeOutput] error=[$probeError]"
        }
        if ($evidence -notmatch "RTC ICE restart started, revision=$restartRevision") {
            throw 'native workspace did not start the RTC restart'
        }
        if ($evidence -notmatch "Managed RTC ICE restart completed, generation=.+revision=$restartRevision") {
            throw 'managed RTC ICE restart did not recover the active session'
        }
        if (([regex]::Matches($evidence, 'Ignore duplicate/stale RTC ICE configuration, revision=')).Count -lt 2) {
            throw 'duplicate and stale RTC restart revisions were not both ignored'
        }
    }

    $restartResult = if ($RtcRestartAcceptance) { ' restart=completed guards=passed' } else { '' }
    $fallbackResult = if ($BlockTargetUdp) { ' udp_fallback=websocket' } else { '' }
    $audioResult = if ($NetworkType -in @('webrtc_direct', 'webrtc')) {
        'audio=initialized'
    } else {
        'audio=authorized-idle-source'
    }
    Write-Host "NATIVE_AUTH_CASE PASS mode=$Mode route=$NetworkType target=$DeviceId monitors=$ExpectedMonitorCount transport=connected video=ui-rendered $audioResult file=connected$restartResult$fallbackResult"
    if ($HoldSeconds -gt 0) {
        Write-Host "NATIVE_AUTH_CASE HOLD seconds=$HoldSeconds"
        $holdDeadline = (Get-Date).AddSeconds($HoldSeconds)
        do {
            Start-Sleep -Milliseconds 500
            $process.Refresh()
            if ($process.HasExited) {
                throw "native client exited during coexistence hold, code=$($process.ExitCode)"
            }
        } while ((Get-Date) -lt $holdDeadline)
        Write-Host 'NATIVE_AUTH_CASE HOLD_COMPLETE'
    }
    $exitCode = 0
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    if ($rtcIceEnvironmentSet) {
        [Environment]::SetEnvironmentVariable('PX_RTC_ICE_CONFIG', $null, 'Process')
    }
    if ($firewallRule) {
        Remove-NetFirewallRule -Name $firewallRule -ErrorAction SilentlyContinue
    }
    if ($probeProcess -and -not $probeProcess.HasExited) {
        Stop-Process -Id $probeProcess.Id -Force -ErrorAction SilentlyContinue
    }
    foreach ($temporaryPath in @($probeConfigPath, $probeStdoutPath, $probeStderrPath)) {
        if ($temporaryPath -and (Test-Path -LiteralPath $temporaryPath)) {
            Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
        }
    }
    $validUid = $uid -and $uid -match '^[A-Za-z0-9_-]+$'
    $validGuestHash = $guestTokenHash -and $guestTokenHash -match '^[a-f0-9]{64}$'
    if (($validUid -or $validGuestHash) -and (Test-Path -LiteralPath $MongoExe)) {
        $cleanupUid = if ($validUid) { $uid } else { '' }
        $cleanupGuestHash = if ($validGuestHash) { $guestTokenHash } else { '' }
        $cleanup = @"
var u='$cleanupUid';
var gt='$cleanupGuestHash';
if (u) {
  db.c_connection_ticket.deleteMany({subject_id:u});
  db.c_user_session.deleteMany({subject_id:u});
  db.c_user_group_member.deleteMany({uid:u});
  db.c_user_device.deleteMany({uid:u});
  db.c_user.deleteMany({uid:u});
  db.c_event.deleteMany({`$or:[{actor_id:u},{target_id:u}]});
}
if (gt) { db.c_user_session.deleteMany({token_hash:gt}); }
printjson({users:u?db.c_user.count({uid:u}):0,sessions:u?db.c_user_session.count({subject_id:u}):0,tickets:u?db.c_connection_ticket.count({subject_id:u}):0,guest_sessions:gt?db.c_user_session.count({token_hash:gt}):0});
"@
        & $MongoExe db_gr_console_server --quiet --eval $cleanup
    }
}

exit $exitCode
