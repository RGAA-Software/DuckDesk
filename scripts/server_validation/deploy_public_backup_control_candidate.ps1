#requires -Version 7.0
[CmdletBinding()]
param([string]$ComputerName = '39.71.45.66')

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$machineText = Get-Content -LiteralPath (Join-Path $repositoryRoot '.env/test_machine.md') -Raw -Encoding UTF8
$machinePassword = [regex]::Match($machineText, '(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*主机名\s*[:：]\s*(.+?)\s*$').Groups[1].Value
if (-not $machinePassword -or -not $machineName) { throw 'Public test host credential is incomplete.' }
$credential = [pscredential]::new("$machineName\Administrator", (ConvertTo-SecureString $machinePassword -AsPlainText -Force))
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$winRmWasRunning = (Get-Service WinRM).Status -eq 'Running'
if (-not $winRmWasRunning) { Start-Service WinRM }
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$remoteSession = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $remoteSession = New-PSSession -ComputerName $ComputerName -Credential $credential
    Invoke-Command -Session $remoteSession -ScriptBlock {
        $ErrorActionPreference = 'Stop'
        $packageRoot = 'D:\PixelsServer\staging\backup-control-90-20260927'
        $configRoot = 'C:\ProgramData\Pixels\Server\config'
        $dataRoot = 'C:\ProgramData\Pixels\Server\data'
        $consolePath = Join-Path $configRoot 'console.env'
        $backupPath = Join-Path $configRoot 'backup.json'
        $relayPath = Join-Path $configRoot 'relay.env'
        $consoleOriginal = [IO.File]::ReadAllText($consolePath)
        $backupOriginal = [IO.File]::ReadAllText($backupPath)
        if ($consoleOriginal -match '(?m)^PIXELS_CONSOLE_BACKUP_CONTROL_TOKEN=' -or
            (ConvertFrom-Json $backupOriginal).PSObject.Properties.Name -contains 'control') {
            throw 'Backup control is already configured; refusing to rotate its token.'
        }
        $consoleBackupPath = Join-Path $configRoot 'console.env.pre-backup-control-20260927'
        $backupBackupPath = Join-Path $configRoot 'backup.json.pre-backup-control-20260927'
        if ((Test-Path -LiteralPath $consoleBackupPath) -or (Test-Path -LiteralPath $backupBackupPath)) {
            throw 'Backup-control configuration snapshot already exists.'
        }
        $relayEnvironment = [IO.File]::ReadAllText($relayPath)
        $caMatch = [regex]::Match($relayEnvironment, '(?m)^PIXELS_RELAY_CONSOLE_CA_FILE=(.+?)\r?$')
        if (-not $caMatch.Success -or -not (Test-Path -LiteralPath $caMatch.Groups[1].Value -PathType Leaf)) {
            throw 'Existing Relay Console CA is unavailable.'
        }
        $manifestHash = (Get-FileHash -LiteralPath (Join-Path $packageRoot 'sha256.json') -Algorithm SHA256).Hash.ToLowerInvariant()
        & (Join-Path $packageRoot 'install.ps1') -PackageRoot $packageRoot -ExpectedManifestSha256 $manifestHash `
            -ConfigRoot $configRoot -DataRoot $dataRoot -PreflightOnly
        if (-not $?) { throw 'Candidate package preflight failed.' }
        Copy-Item -LiteralPath $consolePath -Destination $consoleBackupPath
        Copy-Item -LiteralPath $backupPath -Destination $backupBackupPath
        Get-Acl -LiteralPath $consolePath | Set-Acl -LiteralPath $consoleBackupPath
        Get-Acl -LiteralPath $backupPath | Set-Acl -LiteralPath $backupBackupPath
        $tokenBytes = [byte[]]::new(32)
        $randomGenerator = [Security.Cryptography.RandomNumberGenerator]::Create()
        try { $randomGenerator.GetBytes($tokenBytes) } finally { $randomGenerator.Dispose() }
        $controlToken = ([BitConverter]::ToString($tokenBytes)).Replace('-', '').ToLowerInvariant()
        $backupConfig = ConvertFrom-Json $backupOriginal
        $backupConfig | Add-Member -NotePropertyName control -NotePropertyValue ([pscustomobject]@{
            console_url = 'wss://localhost:4600/api/console/backup-control'
            console_ca_file = $caMatch.Groups[1].Value
            token = $controlToken
        })
        $utf8 = [Text.UTF8Encoding]::new($false)
        try {
            [IO.File]::WriteAllText($consolePath, $consoleOriginal.TrimEnd("`r", "`n") + "`nPIXELS_CONSOLE_BACKUP_CONTROL_TOKEN=$controlToken`n", $utf8)
            [IO.File]::WriteAllText($backupPath, ($backupConfig | ConvertTo-Json -Depth 100) + "`n", $utf8)
            & (Join-Path $packageRoot 'install.ps1') -PackageRoot $packageRoot -ExpectedManifestSha256 $manifestHash `
                -ConfigRoot $configRoot -DataRoot $dataRoot
            if (-not $?) { throw 'Candidate installation failed.' }
        } catch {
            [IO.File]::WriteAllText($consolePath, $consoleOriginal, $utf8)
            [IO.File]::WriteAllText($backupPath, $backupOriginal, $utf8)
            throw
        } finally {
            [Array]::Clear($tokenBytes, 0, $tokenBytes.Length)
        }
        [pscustomobject]@{
            deployment = [string]$backupConfig.deployment_id
            candidate_manifest_sha256 = $manifestHash
            services = @(Get-Service 'Pixels.Console', 'Pixels.Relay', 'Pixels.Backup.*' | Select-Object Name, Status)
            config_snapshots = @($consoleBackupPath, $backupBackupPath)
        }
    }
} finally {
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
    if (-not $winRmWasRunning) { Stop-Service WinRM }
}
