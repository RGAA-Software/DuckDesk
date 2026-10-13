param(
    [Parameter(Mandatory)] [string]$Workspace,
    [Parameter(Mandatory)] [string]$ExpectedClientSha256,
    [string]$ClientRoot = 'C:\Program Files\Pixels Client',
    [ValidateRange(30, 300)] [int]$HoldSeconds = 45
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$clientPath = Join-Path $ClientRoot 'px_client.exe'
if ((Get-FileHash -LiteralPath $clientPath).Hash -ine $ExpectedClientSha256) { throw 'Installed Client identity differs' }
$launch = [Console]::In.ReadToEnd() | ConvertFrom-Json
if ($launch -is [array] -or $launch.schema -ne 1 -or $launch.rdp -or -not $launch.frontend_session_id) {
    throw 'Expected one Native media launch description'
}
[void][IO.Directory]::CreateDirectory($Workspace)
$resultPath = Join-Path $Workspace 'client-batch.json'
if (Test-Path -LiteralPath $resultPath) { throw 'Preserve the existing validation result' }
$options = [Diagnostics.ProcessStartInfo]::new()
$options.FileName = $clientPath
$options.Arguments = '--native-launch-stdin'
$options.WorkingDirectory = $ClientRoot
$options.UseShellExecute = $false
$options.RedirectStandardInput = $true
$options.CreateNoWindow = $true
$client = [Diagnostics.Process]::new()
$client.StartInfo = $options
$report = [ordered]@{ instance_id = $launch.connection_instance_id; session_id = $launch.frontend_session_id; state = 'not_started' }
try {
    if (-not $client.Start()) { throw 'Native Client failed to start' }
    $report.pid = $client.Id
    $report.state = 'started'
    $report.started_at_utc = [DateTimeOffset]::UtcNow.ToString('o')
    $report.process_created_utc = $client.StartTime.ToUniversalTime().ToString('o')
    $client.StandardInput.Write(($launch | ConvertTo-Json -Depth 12 -Compress))
    $client.StandardInput.Close()
    $launch = $null
    $holdClock = [Diagnostics.Stopwatch]::StartNew()
    while ($holdClock.Elapsed.TotalSeconds -lt $HoldSeconds) {
        if ($client.HasExited) { throw 'Owned Native Client exited before the sample completed' }
        Start-Sleep -Milliseconds 500
    }
    $report.hold_seconds = $holdClock.Elapsed.TotalSeconds
} finally {
    if ($report.state -eq 'started') {
        if (-not $client.HasExited) {
            [void]$client.CloseMainWindow()
            if (-not $client.WaitForExit(10000)) { $client.Kill(); [void]$client.WaitForExit(10000) }
        }
        $report.state = 'closed'
        $report.closed_at_utc = [DateTimeOffset]::UtcNow.ToString('o')
    }
    $client.Dispose()
    [IO.File]::WriteAllText($resultPath, ($report | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
    $report | ConvertTo-Json -Depth 5 -Compress
}
