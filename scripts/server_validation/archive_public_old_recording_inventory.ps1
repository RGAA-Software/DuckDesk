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
        $inventoryPath = 'C:\Users\Public\Pixels\px_data\recording_inventory.json'
        $expectedHash = 'A8B7CB184E3E0E546DFDFDDD405FAA8D9C61A0FE4DCAB39387517B0E98876263'
        if ((Get-FileHash -LiteralPath $inventoryPath -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'The reviewed old recording inventory changed; refusing to archive.'
        }
        $archiveDirectory = 'D:\PixelsServer\backups\old-node-recording-inventory-20260926'
        if (Test-Path -LiteralPath $archiveDirectory) { throw 'Archive directory already exists.' }
        New-Item -ItemType Directory -Path $archiveDirectory -ErrorAction Stop | Out-Null
        Stop-Service -Name px_service -ErrorAction Stop
        (Get-Service -Name px_service).WaitForStatus('Stopped', [timespan]::FromSeconds(30))
        try {
            Move-Item -LiteralPath $inventoryPath -Destination $archiveDirectory -ErrorAction Stop
        } finally {
            Start-Service -Name px_service -ErrorAction Stop
            (Get-Service -Name px_service).WaitForStatus('Running', [timespan]::FromSeconds(30))
        }
        $archivedPath = Join-Path $archiveDirectory 'recording_inventory.json'
        if ((Get-FileHash -LiteralPath $archivedPath -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'Archived recording inventory hash differs.'
        }
        [pscustomobject]@{
            result = 'OLD_INVENTORY_ARCHIVED'
            archive_path = $archivedPath
            sha256 = $expectedHash
            service = (Get-Service -Name px_service).Status.ToString()
        } | ConvertTo-Json -Compress
    }
} finally {
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
