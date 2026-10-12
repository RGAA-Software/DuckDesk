param(
    [Parameter(Mandatory)][string]$Workspace,
    [string]$ClientRoot = 'C:\Program Files\Pixels Client',
    [string]$ExpectedClientSha256 = 'FE8B82574E133D765D2FA2E7838370F2AEC761E0FCA8363DF9C701DB96B92466',
    [ValidateRange(30,900)][int]$HoldSeconds = 600
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$clientPath = Join-Path $ClientRoot 'px_client.exe'
if ((Get-FileHash -LiteralPath $clientPath).Hash -ine $ExpectedClientSha256) { throw 'Installed Client identity differs' }
$launches = [Console]::In.ReadToEnd() | ConvertFrom-Json
if ($launches.Count -lt 1 -or $launches.Count -gt 5) { throw 'Expected one to five RDP launch descriptions' }
[void][IO.Directory]::CreateDirectory($Workspace)
$resultPath = Join-Path $Workspace 'client-batch.json'
$ownedClients = [Collections.Generic.List[Diagnostics.Process]]::new()
$reports = [Collections.Generic.List[object]]::new()
try {
    foreach ($launch in $launches) {
        if ($launch.schema -ne 1 -or $launch -is [array] -or -not $launch.rdp -or -not $launch.frontend_session_id) {
            throw 'RDP launch contract missing or nested'
        }
        $options = [Diagnostics.ProcessStartInfo]::new()
        $options.FileName = $clientPath
        $options.Arguments = '--rdp-launch-stdin'
        $options.WorkingDirectory = $ClientRoot
        $options.UseShellExecute = $false
        $options.RedirectStandardInput = $true
        $options.CreateNoWindow = $true
        $client = [Diagnostics.Process]::new()
        $client.StartInfo = $options
        if (-not $client.Start()) { $client.Dispose(); throw 'RDP Client failed to start' }
        $ownedClients.Add($client)
        $client.StandardInput.Write(($launch | ConvertTo-Json -Depth 12 -Compress))
        $client.StandardInput.Close()
        $reports.Add([ordered]@{pid=$client.Id;instance_id=$launch.connection_instance_id;session_id=$launch.frontend_session_id;
            account_name=$launch.rdp.account_name;state='started';started_at_utc=[DateTimeOffset]::UtcNow.ToString('o')})
    }
    $launches = $null
    [IO.File]::WriteAllText($resultPath,($reports.ToArray() | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    $holdClock = [Diagnostics.Stopwatch]::StartNew()
    while ($holdClock.Elapsed.TotalSeconds -lt $HoldSeconds) {
        foreach ($client in $ownedClients) {
            $client.Refresh()
            if ($client.HasExited) { throw 'An owned RDP Client exited before the concurrent sample completed' }
        }
        Start-Sleep -Milliseconds 500
    }
} finally {
    for ($clientIndex=0; $clientIndex -lt $ownedClients.Count; $clientIndex++) {
        $client = $ownedClients[$clientIndex]
        if (-not $client.HasExited) {
            [void]$client.CloseMainWindow()
            if (-not $client.WaitForExit(10000)) { $client.Kill(); [void]$client.WaitForExit(10000) }
        }
        $reports[$clientIndex].state = 'closed'
        $reports[$clientIndex].closed_at_utc = [DateTimeOffset]::UtcNow.ToString('o')
        $client.Dispose()
    }
    [IO.File]::WriteAllText($resultPath,($reports.ToArray() | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    $reports.ToArray() | ConvertTo-Json -Depth 5 -Compress
}
