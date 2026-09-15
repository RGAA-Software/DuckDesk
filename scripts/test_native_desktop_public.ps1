#requires -Version 7.0

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66',
    [ValidateRange(10, 120)]
    [int]$TimeoutSeconds = 35,
    [ValidateRange(5, 180)]
    [int]$StaticHoldSeconds = 35
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$clientPath = Join-Path $repository 'build_official/dist/px_client.exe'
$buildClientPath = Join-Path $repository 'build_official/src/px_deps/px_client.exe'
$licensePath = Join-Path $repository '.env/public_license.json'
$machinePath = Join-Path $repository '.env/test_machine.md'
$levelDbTool = 'C:/source/vcpkg/buildtrees/leveldb/x64-windows-rel/leveldbutil.exe'
$clientLogPath = Join-Path (Split-Path $clientPath -Parent) 'px_logs/px_client.log'

foreach ($path in @($clientPath, $buildClientPath, $licensePath, $machinePath, $levelDbTool)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Desktop acceptance input is missing: $path"
    }
}
if ((Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $buildClientPath -Algorithm SHA256).Hash) {
    throw 'The official Client and build-tree artifact hashes differ.'
}

$machineText = Get-Content -LiteralPath $machinePath -Raw
$nodePassword = [regex]::Match($machineText, '(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*主机名\s*[:：]\s*(.+?)\s*$').Groups[1].Value
if (-not $nodePassword -or -not $machineName) {
    throw 'Public test host machine-qualified deployment credential is incomplete.'
}
$credential = [pscredential]::new("$machineName\Administrator", (ConvertTo-SecureString $nodePassword -AsPlainText -Force))
$license = Get-Content -LiteralPath $licensePath -Raw | ConvertFrom-Json
$previousTrustedHosts = (Get-Item WSMan:\localhost\Client\TrustedHosts).Value
$preferenceSnapshot = Join-Path $env:TEMP "pixels-public-desktop-$PID-$([guid]::NewGuid().ToString('N'))"
$clientLogOffset = if (Test-Path -LiteralPath $clientLogPath) { (Get-Item -LiteralPath $clientLogPath).Length } else { 0L }
$session = $null
$client = $null

function Read-NewClientLog {
    if (-not (Test-Path -LiteralPath $clientLogPath)) {
        return ''
    }
    $stream = [IO.File]::Open($clientLogPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        [void]$stream.Seek([Math]::Min($clientLogOffset, $stream.Length), [IO.SeekOrigin]::Begin)
        $reader = [IO.StreamReader]::new($stream)
        try {
            return $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

try {
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $ComputerName -Force
    $session = New-PSSession -ComputerName $ComputerName -Credential $credential
    $remotePreferences = @(Invoke-Command -Session $session -ScriptBlock {
        Get-ChildItem 'C:\Users\Public\Pixels\px_data\pixels.dat' -Filter '*.ldb' |
            Sort-Object LastWriteTime -Descending |
            Select-Object -ExpandProperty FullName
    })
    if ($remotePreferences.Count -eq 0) {
        throw 'No public test host preference database was found.'
    }
    [void](New-Item -ItemType Directory -Path $preferenceSnapshot -Force)
    foreach ($remotePreference in $remotePreferences) {
        Copy-Item -FromSession $session -LiteralPath $remotePreference -Destination $preferenceSnapshot -Force
    }

    Push-Location $preferenceSnapshot
    try {
        $dump = @(Get-ChildItem -LiteralPath $preferenceSnapshot -Filter '*.ldb' | ForEach-Object {
            & $levelDbTool dump $_.Name 2>&1
        }) -join "`n"
    } finally {
        Pop-Location
    }
    $passwordEntries = @([regex]::Matches($dump, "'device_random_pwd'\s+@\s+(\d+)\s+:\s+val\s+=>\s+'([^'\r\n]+)'") | ForEach-Object {
        [pscustomobject]@{ Sequence = [uint64]$_.Groups[1].Value; Value = $_.Groups[2].Value }
    } | Sort-Object Sequence -Descending)
    if ($passwordEntries.Count -eq 0) {
        throw 'The node temporary password was not found in the preference snapshot.'
    }
    $remotePassword = $passwordEntries[0].Value.Trim()
    $deviceIdEntries = @([regex]::Matches($dump, "'device_id'\s+@\s+(\d+)\s+:\s+val\s+=>\s+'([^'\r\n]+)'") | ForEach-Object {
        [pscustomobject]@{ Sequence = [uint64]$_.Groups[1].Value; Value = $_.Groups[2].Value }
    } | Sort-Object Sequence -Descending)
    if ($deviceIdEntries.Count -eq 0) {
        throw 'The node device ID was not found in the preference snapshot.'
    }
    $remoteDeviceId = $deviceIdEntries[0].Value.Trim()
    $algorithm = [Security.Cryptography.MD5]::Create()
    try {
        $passwordHash = ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($remotePassword)))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
        $remotePassword = $null
        $dump = $null
    }

    $nonce = [guid]::NewGuid().ToString('N')
    $launch = @{
        schema = 1
        host = $ComputerName
        port = 4601
        stream_id = "direct-$nonce"
        connection_instance_id = "direct-$nonce"
        connection_nonce = $nonce
        device_id = "desktop_acceptance_$nonce"
        remote_device_id = $remoteDeviceId
        remote_password_hash = $passwordHash
        mode = 'desktop'
        appkey = [string]$license.appkey
        stream_name = 'Public desktop UDP acceptance'
        language = 'zh-CN'
        decoder = 'Auto'
        audio = $true
        clipboard = $true
        only_viewing = $false
        force_tcp = $false
        force_relay = $false
        split_windows = $false
    }
    $envelope = $launch | ConvertTo-Json -Compress -Depth 8
    $launch = $null
    $start = [Diagnostics.ProcessStartInfo]::new($clientPath, '--native-launch-stdin')
    $start.WorkingDirectory = Split-Path $clientPath -Parent
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $client = [Diagnostics.Process]::Start($start)
    $stdoutDrain = $client.StandardOutput.ReadToEndAsync()
    $stderrDrain = $client.StandardError.ReadToEndAsync()
    $client.StandardInput.Write($envelope)
    $client.StandardInput.Close()
    $envelope = $null
    $passwordHash = $null

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $evidence = ''
    do {
        Start-Sleep -Milliseconds 250
        $client.Refresh()
        if ($client.HasExited) {
            throw "Desktop Client exited before acceptance: exit=$($client.ExitCode)"
        }
        $evidence = Read-NewClientLog
        $hasDeliveredFrame = $evidence -match 'UDP media v2 video delivered:'
        $hasDecodedFrame = $evidence -match '\[LAT-decode\].*frames=[1-9][0-9]*|Video frame (came|stream reset)'
    } while ((-not $hasDeliveredFrame -or -not $hasDecodedFrame) -and [DateTime]::UtcNow -lt $deadline)

    if (-not $hasDeliveredFrame -or -not $hasDecodedFrame) {
        throw "Desktop UDP acceptance timed out: delivered=$hasDeliveredFrame decoded=$hasDecodedFrame"
    }
    Start-Sleep -Seconds $StaticHoldSeconds
    $evidence = Read-NewClientLog
    $positiveWindow = $evidence -match 'UDP media v2 window: frames=[1-9][0-9]*'
    $watchdogTimeout = $evidence -match 'Udp direct watchdog timeout'
    $mediaUnavailable = $evidence -match 'UDP media unavailable'
    if ($watchdogTimeout -or $mediaUnavailable) {
        throw "Desktop UDP became unavailable during the ${StaticHoldSeconds}s static hold: watchdog=$watchdogTimeout mediaUnavailable=$mediaUnavailable"
    }
    $filteredEvidence = @($evidence -split "`r?`n" | Where-Object {
        $_ -match 'Native media transport|UDP datagram profile|UDP media v2 video delivered|UDP media v2 window|UDP video totals|LAT-decode|Video frame'
    } | Select-Object -Last 40)
    [pscustomobject]@{
        Result = 'PASS'
        Mode = 'Desktop UDP/FEC'
        Endpoint = "$ComputerName`:4601"
        RemoteDeviceId = $remoteDeviceId
        DeliveredFrame = $hasDeliveredFrame
        DecodedFrame = $hasDecodedFrame
        PositiveFiveSecondWindow = $positiveWindow
        StaticHoldSeconds = $StaticHoldSeconds
        WatchdogTimeout = $watchdogTimeout
        MediaUnavailable = $mediaUnavailable
        ClientHash = (Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash
        Evidence = $filteredEvidence -join "`n"
    }
} finally {
    if ($client -and -not $client.HasExited) {
        $client.Kill()
        [void]$client.WaitForExit(5000)
    }
    if ($session) {
        Remove-PSSession $session
    }
    if ($preferenceSnapshot.StartsWith($env:TEMP, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $preferenceSnapshot -Recurse -Force -ErrorAction SilentlyContinue
    }
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $previousTrustedHosts -Force
}
