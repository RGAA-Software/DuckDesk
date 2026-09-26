#requires -Version 5.1

[CmdletBinding()]
param([string]$ComputerName = '39.71.45.66')

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$machineText = Get-Content -LiteralPath (Join-Path $repositoryRoot '.env/test_machine.md') -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) { throw 'Public test host credential is incomplete.' }
$credential = [pscredential]::new("$machineName\Administrator", (ConvertTo-SecureString $password -AsPlainText -Force))
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$remoteSession = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $remoteSession = New-PSSession -ComputerName $ComputerName -Credential $credential
    Invoke-Command -Session $remoteSession -ScriptBlock {
        $nodeServices = @(Get-CimInstance Win32_Service | Where-Object {
            $_.PathName -like '*Pixels Cloud Node*' -or $_.Name -like '*px_service*'
        } | Select-Object Name, State, StartName, PathName)
        $nodeControlDirectories = @(
            'C:\Users\Public\Pixels\px_data\node-control',
            'C:\Users\Public\Pixels\px_logs'
        ) | ForEach-Object {
            [pscustomobject]@{ path = $_; exists = Test-Path -LiteralPath $_ }
        }
        $logDirectory = 'C:\Users\Public\Pixels\px_logs'
        $recentLogs = if (Test-Path -LiteralPath $logDirectory) {
            @(Get-ChildItem -LiteralPath $logDirectory -File | Sort-Object LastWriteTime -Descending |
                Select-Object -First 5 Name, LastWriteTime, Length)
        } else { @() }
        $serviceLog = Join-Path $logDirectory 'pixels_service.log'
        $inventoryPath = 'C:\Users\Public\Pixels\px_data\recording_inventory.json'
        $inventorySummary = if (Test-Path -LiteralPath $inventoryPath -PathType Leaf) {
            $inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
            [pscustomobject]@{
                path = $inventoryPath
                sha256 = (Get-FileHash -LiteralPath $inventoryPath -Algorithm SHA256).Hash
                entry_count = @($inventory.entries).Count
            }
        } else { $null }
        $recentControlEvents = if (Test-Path -LiteralPath $serviceLog) {
            @(Get-Content -LiteralPath $serviceLog -Tail 250 | Where-Object {
                $_ -match 'node.control|node_control|certificate|tls|websocket'
            } | Select-Object -Last 15 | ForEach-Object {
                [regex]::Replace([string]$_, '\b[0-9a-f]{64}\b', '[REDACTED]')
            })
        } else { @() }
        [pscustomobject]@{
            services = $nodeServices
            node_control_directories = $nodeControlDirectories
            recent_logs = $recentLogs
            recent_control_events = $recentControlEvents
            recording_inventory = $inventorySummary
        } | ConvertTo-Json -Depth 5 -Compress
    }
} finally {
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
