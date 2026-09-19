#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66',
    [ValidateRange(1, 100)]
    [int]$MaximumItems = 20
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$machineFile = Join-Path $repository '.env/test_machine.md'
$machineText = Get-Content -LiteralPath $machineFile -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) {
    throw 'Public test host machine-qualified credential is incomplete.'
}

$credential = [pscredential]::new(
    "$machineName\Administrator",
    (ConvertTo-SecureString $password -AsPlainText -Force)
)
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$session = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $session = New-PSSession -ComputerName $ComputerName -Credential $credential
    Invoke-Command -Session $session -ArgumentList $MaximumItems -ScriptBlock {
        param($maximumItems)

        $sharedRoot = 'C:\Users\Public\Pixels'
        $recordingRoot = Join-Path $sharedRoot 'px_render_records'
        $inventoryPath = Join-Path $sharedRoot 'px_data\recording_inventory.json'
        $logRoot = Join-Path $sharedRoot 'px_logs'
        $recordings = if (Test-Path -LiteralPath $recordingRoot -PathType Container) {
            @(Get-ChildItem -LiteralPath $recordingRoot -Filter '*.mp4' -File |
                Sort-Object LastWriteTimeUtc -Descending |
                Select-Object -First $maximumItems |
                ForEach-Object {
                    [pscustomobject]@{
                        FileName = $_.Name
                        SizeBytes = $_.Length
                        LastWriteTimeUtc = $_.LastWriteTimeUtc
                        Sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
                        Finalized = -not (Test-Path -LiteralPath ($_.FullName + '.recording') -PathType Leaf)
                    }
                })
        }
        else {
            @()
        }
        $inventory = if (Test-Path -LiteralPath $inventoryPath -PathType Leaf) {
            $document = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
            @($document.entries | Select-Object -First $maximumItems | ForEach-Object {
                [pscustomobject]@{
                    SourceId = $_.source_id
                    SessionId = $_.session_id
                    FileName = $_.file_name
                    SizeBytes = $_.size_bytes
                    Sequence = $_.sequence
                    Present = $_.present
                }
            })
        }
        else {
            @()
        }
        $logEvidence = if (Test-Path -LiteralPath $logRoot -PathType Container) {
            @(Get-ChildItem -LiteralPath $logRoot -Filter '*.log' -File |
                Sort-Object LastWriteTimeUtc -Descending |
                Select-Object -First 6 |
                ForEach-Object {
                    $logName = $_.Name
                    Get-Content -LiteralPath $_.FullName -Tail 2000 |
                        Select-String -Pattern 'recording|record\.' |
                        Where-Object { $_.Line -notmatch '(?i)token|password|secret|appkey|authorization' } |
                        Select-Object -Last $maximumItems |
                        ForEach-Object { "$logName`: $($_.Line)" }
                })
        }
        else {
            @()
        }
        [pscustomobject]@{
            Recordings = $recordings
            Inventory = $inventory
            LogEvidence = $logEvidence
        }
    }
}
finally {
    if ($session) {
        Remove-PSSession $session
    }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
