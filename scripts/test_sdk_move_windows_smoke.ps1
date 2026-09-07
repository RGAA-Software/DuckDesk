param([ValidateRange(10, 60)][int]$Seconds = 20)

$ErrorActionPreference = 'Stop'
$smokeRepo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$smokeDist = Join-Path $smokeRepo 'build_official/dist'
if (Get-Process -Name px_client -ErrorAction SilentlyContinue) {
    throw 'Close the existing remote-control client before running this local smoke test.'
}
$render = Get-CimInstance Win32_Process -Filter "Name='px_render.exe'" |
    Where-Object { $_.ExecutablePath -eq (Join-Path $smokeDist 'px_render.exe') } |
    Select-Object -First 1
if (-not $render) { throw 'The local dist Render must already be running.' }

function Read-RenderArgument([string]$Name) {
    $match = [regex]::Match($render.CommandLine, ('--' + [regex]::Escape($Name) + '(?:=|\s+)(?:"([^"]*)"|([^\s]+))'))
    if (-not $match.Success) { return '' }
    if ($match.Groups[1].Success) { return $match.Groups[1].Value }
    if ($match.Groups[2].Value.StartsWith('--')) { return '' }
    return $match.Groups[2].Value
}

# Use the locally running test host's credential in memory only. Never print it,
# persist it, or pass it to the client. The child receives a one-time stream binding.
$port = [int](Read-RenderArgument 'network_listen_port')
if ($port -lt 1 -or $port -gt 65535) { throw 'Invalid local Render port.' }
$passwordDigest = Read-RenderArgument 'device_safety_pwd'
if (-not $passwordDigest) {
    $randomPassword = Read-RenderArgument 'device_random_pwd'
    if ($randomPassword) {
        $md5 = [Security.Cryptography.MD5]::Create()
        try { $passwordDigest = [Convert]::ToHexString($md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($randomPassword))).ToLowerInvariant() }
        finally { $md5.Dispose() }
    }
}
$nonce = [guid]::NewGuid().ToString('N')
try {
    $authorization = Invoke-RestMethod -TimeoutSec 5 -Uri (
        "http://127.0.0.1:$port/verify/security/password?safety_pwd_md5=$passwordDigest&client_nonce=$nonce")
} catch {
    throw 'Local Render authorization request failed; credential-bearing URL omitted.'
}
$passwordDigest = $null
$randomPassword = $null
$render = $null
if ($authorization.code -ne 200) { throw "Local direct authorization rejected: $($authorization.code)" }
$grant = $authorization.data
if ($grant -is [string]) { $grant = $grant | ConvertFrom-Json }
if (-not $grant.stream_id) { throw 'Render did not issue a one-time stream binding.' }

$logPath = Join-Path ([Environment]::GetFolderPath('CommonDocuments') | Split-Path -Parent) 'Pixels/px_logs/app.127.0.0.1.log'
$logOffset = if (Test-Path -LiteralPath $logPath) { (Get-Item -LiteralPath $logPath).Length } else { 0L }
$client = $null
try {
    $client = Start-Process -FilePath (Join-Path $smokeDist 'px_client.exe') -WorkingDirectory $smokeDist -PassThru -ArgumentList @(
        '--host=127.0.0.1', "--port=$port", '--conn_type=direct',
        "--stream_id=$($grant.stream_id)", "--connection_nonce=$nonce", '--device_id=sdk-layout-smoke',
        '--audio=1', '--clipboard=0', '--only_viewing=1', '--language=0')
    Start-Sleep -Seconds $Seconds
    $client.Refresh()
    if ($client.HasExited) { throw "Windows client exited during the smoke test: $($client.ExitCode)" }
    $stream = [IO.File]::Open($logPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -ge $logOffset) { [void]$stream.Seek($logOffset, [IO.SeekOrigin]::Begin) }
        $reader = [IO.StreamReader]::new($stream)
        try { $log = $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
    $result = [ordered]@{
        processAlive = $true
        windowCreated = $client.MainWindowHandle -ne 0
        udpMediaReady = $log.Contains('Udp direct first media received')
        # Hardware decoders do not emit the software decoder's creation message.
        # Require successive decode samples instead of a backend-specific log line.
        framesDecoded = [regex]::Matches($log, '\[LAT-decode\] frames=[1-9][0-9]*').Count -ge 2
        decodeErrors = $log -match 'decode error:|Video decoder produced an error|Don.t have decoded image'
        seconds = $Seconds
    }
    $result | ConvertTo-Json | Set-Content (Join-Path $smokeRepo 'test-results/sdk-move-windows-smoke.json')
    $result | ConvertTo-Json
    if (-not ($result.windowCreated -and $result.udpMediaReady -and $result.framesDecoded -and -not $result.decodeErrors)) {
        throw 'Windows smoke test did not prove UDP media and decoded frames.'
    }
} finally {
    if ($client -and -not $client.HasExited) {
        [void]$client.CloseMainWindow()
        if (-not $client.WaitForExit(5000)) { Stop-Process -Id $client.Id -Force }
    }
}
