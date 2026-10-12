param(
    [Parameter(Mandatory = $true)][string]$Workspace,
    [Parameter(Mandatory = $true)][string]$ClientRoot,
    [Parameter(Mandatory = $true)][string]$ExpectedClientSha256,
    [string]$ExpectedRelay,
    [switch]$RequireDirect,
    [ValidateRange(30, 1800)][int]$SampleSeconds = 600
)
$ErrorActionPreference = 'Stop'
if ([bool]$ExpectedRelay -eq [bool]$RequireDirect) { throw 'Select exactly one expected Relay or direct path' }
$clientPath = Join-Path $ClientRoot 'px_client.exe'
$actualHash = (Get-FileHash -LiteralPath $clientPath -Algorithm SHA256).Hash
if ($actualHash -ne $ExpectedClientSha256) { throw 'Client identity mismatch' }
if (Get-Process px_client -ErrorAction SilentlyContinue) { throw 'An existing Client must remain undisturbed' }
$launchPath = Join-Path $Workspace 'launch.json'
$resultPath = Join-Path $Workspace 'video-result.json'
$sampleStart = [DateTime]::Now
$result = [ordered]@{state='starting';requested_seconds=$SampleSeconds;client_sha256=$actualHash;expected_relay=$ExpectedRelay;require_direct=[bool]$RequireDirect}
$processOptions = [Diagnostics.ProcessStartInfo]::new()
$processOptions.FileName = $clientPath
$processOptions.Arguments = '--native-launch-stdin'
$processOptions.WorkingDirectory = $ClientRoot
$processOptions.UseShellExecute = $false
$processOptions.RedirectStandardInput = $true
$processOptions.CreateNoWindow = $true
$ownedClient = [Diagnostics.Process]::new()
$ownedClient.StartInfo = $processOptions
$clientStarted = $false
$logDirectories = @((Join-Path $ClientRoot 'px_logs'), 'C:\Users\Public\Pixels\px_logs')
function Read-CurrentClientLog {
    $currentLines = [Collections.Generic.List[string]]::new()
    foreach ($logDirectory in $logDirectories) {
        if (-not (Test-Path -LiteralPath $logDirectory)) { continue }
        foreach ($logFile in Get-ChildItem -LiteralPath $logDirectory -Filter 'px_client*.log' -File) {
            $inputStream = [IO.FileStream]::new($logFile.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            $logReader = [IO.StreamReader]::new($inputStream)
            try {
                while ($null -ne ($logLine = $logReader.ReadLine())) {
                    if ($logLine.Length -ge 25 -and $logLine[0] -eq '[' -and
                        [string]::CompareOrdinal($logLine.Substring(1,23), $sampleStart.ToString('yyyy-MM-dd HH:mm:ss.fff')) -ge 0) {
                        $currentLines.Add($logLine)
                    }
                }
            } finally { $logReader.Dispose(); $inputStream.Dispose() }
        }
    }
    return $currentLines.ToArray()
}
function Save-Result {
    [IO.File]::WriteAllText($resultPath, ($result | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
}
try {
    $clientStarted = $ownedClient.Start()
    if (-not $clientStarted) { throw 'Client did not start' }
    $result.client_pid = $ownedClient.Id
    $result.started_local = $sampleStart.ToString('yyyy-MM-dd HH:mm:ss.fff')
    $result.started_epoch = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() / 1000.0
    $ownedClient.StandardInput.Write([IO.File]::ReadAllText($launchPath))
    $ownedClient.StandardInput.Close()
    Remove-Item -LiteralPath $launchPath
    Save-Result
    $readinessDeadline = [DateTime]::UtcNow.AddSeconds(45)
    $expectedPath = if ($RequireDirect) { 'selected={"Ip":"' } else { 'selected={"Relay":"' + $ExpectedRelay + '"}' }
    do {
        if ($ownedClient.HasExited) { throw 'Client exited before first picture' }
        $logText = (Read-CurrentClientLog) -join "`n"
        if ($logText -match 'event=iroh.connect outcome=failed code=([^\s]+)') { throw ('Client connection failed: ' + $Matches[1]) }
        if ($logText -match 'event=video.present phase=window.*?frames=[1-9]' -and $logText.Contains($expectedPath)) { break }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $readinessDeadline)
    if ($logText -notmatch 'event=video.present phase=window.*?frames=[1-9]') { throw 'No current presented video' }
    if (-not $logText.Contains($expectedPath)) { throw 'Actual path differs from the requested transport path' }
    $result.state = 'sampling'
    $result.sample_started_epoch = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() / 1000.0
    Save-Result
    $sampleClock = [Diagnostics.Stopwatch]::StartNew()
    while ($sampleClock.Elapsed.TotalSeconds -lt $SampleSeconds) {
        if ($ownedClient.HasExited) { throw 'Client exited during video' }
        Start-Sleep -Milliseconds 500
    }
    $result.sample_elapsed_seconds = $sampleClock.Elapsed.TotalSeconds
    $result.sample_ended_epoch = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() / 1000.0
    $result.state = 'sample_complete'
} catch {
    $result.state = 'failed'
    $result.failure = $_.Exception.Message
} finally {
    if ($clientStarted -and -not $ownedClient.HasExited) {
        $ownedClient.CloseMainWindow() | Out-Null
        if (-not $ownedClient.WaitForExit(5000)) { $ownedClient.Kill(); $ownedClient.WaitForExit() }
    }
    $result.client_exited = -not $clientStarted -or $ownedClient.HasExited
    $ownedClient.Dispose()
    $rawPath = Join-Path $Workspace 'video-client.log'
    [IO.File]::WriteAllLines($rawPath, @(Read-CurrentClientLog), [Text.UTF8Encoding]::new($false))
    $compressedPath = $rawPath + '.gz'
    $inputLog = [IO.File]::OpenRead($rawPath)
    $outputLog = [IO.File]::Create($compressedPath)
    $compressor = [IO.Compression.GZipStream]::new($outputLog, [IO.Compression.CompressionLevel]::Optimal, $true)
    try { $inputLog.CopyTo($compressor) } finally { $compressor.Dispose(); $outputLog.Dispose(); $inputLog.Dispose() }
    $result.raw_sha256 = (Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash
    $result.gzip_sha256 = (Get-FileHash -LiteralPath $compressedPath -Algorithm SHA256).Hash
    $result.gzip_bytes = (Get-Item -LiteralPath $compressedPath).Length
    Save-Result
    Write-Output ($result | ConvertTo-Json -Depth 5)
}
if ($result.state -ne 'sample_complete') { exit 1 }
