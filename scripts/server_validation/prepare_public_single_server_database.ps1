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
        $backupRoot = 'D:\PixelsServer\backups'
        $postgresClientRoot = 'D:\PixelsServer\postgresql\pgsql\bin'
        $postgresClient = Join-Path $postgresClientRoot 'psql.exe'
        $dumpClient = Join-Path $postgresClientRoot 'pg_dump.exe'
        $restoreClient = Join-Path $postgresClientRoot 'pg_restore.exe'
        foreach ($requiredPath in @($backupRoot, $postgresClient, $dumpClient, $restoreClient,
                'D:\PixelsServer\config\pg-admin-password', 'D:\PixelsServer\app\tls\console-ca.pem')) {
            if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Required PostgreSQL input missing: $requiredPath" }
        }
        if ((Get-Service -Name PixelsPostgreSQL18).Status -ne 'Running' -or
            @(Get-Service -Name 'Pixels.Console', 'Pixels.Relay', 'Pixels.Backup.*' `
                -ErrorAction SilentlyContinue | Where-Object Status -eq Running).Count -ne 0) {
            throw 'PostgreSQL is unavailable or Pixels business services are running.'
        }
        if (@(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | Where-Object {
                $_.LocalPort -in @(4600, 4605, 4700)
            }).Count -ne 0) { throw 'Pixels business/setup listeners are present.' }
        try {
            $env:PGPASSWORD = (Get-Content -LiteralPath 'D:\PixelsServer\config\pg-admin-password' -Raw).Trim()
            $env:PGSSLMODE = 'verify-full'
            $env:PGSSLROOTCERT = 'D:\PixelsServer\app\tls\console-ca.pem'
            $commonArguments = @('-X', '-A', '-t', '-h', 'localhost', '-p', '54329', '-U', 'postgres', '-d', 'postgres',
                '-v', 'ON_ERROR_STOP=1')
            $databaseCount = [int](& $postgresClient @commonArguments -c `
                "SELECT count(*) FROM pg_database WHERE datname = 'pixels_console'")
            $activeConnections = [int](& $postgresClient @commonArguments -c `
                "SELECT count(*) FROM pg_stat_activity WHERE datname = 'pixels_console'")
            $otherDependencies = [int](& $postgresClient @commonArguments -c `
                "SELECT count(*) FROM pg_shdepend WHERE refobjid IN (SELECT oid FROM pg_roles WHERE rolname IN ('pixels_console_owner','pixels_console_runtime','pixels_console_backup')) AND dbid NOT IN (0, (SELECT oid FROM pg_database WHERE datname='pixels_console'))")
            $roleNames = @(& $postgresClient @commonArguments -c `
                "SELECT rolname FROM pg_roles WHERE rolname LIKE 'pixels_console_%' ORDER BY rolname")
            $expectedRoles = @('pixels_console_backup', 'pixels_console_owner', 'pixels_console_runtime')
            if ($databaseCount -ne 1 -or $activeConnections -ne 0 -or $otherDependencies -ne 0 -or
                (@($roleNames) -join ',') -ne ($expectedRoles -join ',')) {
                throw 'Existing Console database/roles are not the isolated inactive development shape.'
            }
            $backupName = 'single-server-before-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' +
                [guid]::NewGuid().ToString('N').Substring(0, 8)
            $backupDirectory = Join-Path $backupRoot $backupName
            New-Item -ItemType Directory -Path $backupDirectory -ErrorAction Stop | Out-Null
            $archivePath = Join-Path $backupDirectory 'pixels_console.dump'
            & $dumpClient -h localhost -p 54329 -U postgres -d pixels_console --format custom `
                --file $archivePath --no-password
            if ($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $archivePath).Length -le 0) {
                throw 'Console database backup failed; existing database was retained.'
            }
            & $restoreClient --list $archivePath | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Console database backup inventory failed; existing database was retained.' }
            $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
            [pscustomobject]@{
                database = 'pixels_console'
                archive_sha256 = $archiveHash
                archive_bytes = (Get-Item -LiteralPath $archivePath).Length
                created_at_utc = [datetime]::UtcNow.ToString('o')
            } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backupDirectory 'backup-manifest.json') -Encoding UTF8
            & $postgresClient @commonArguments -c 'DROP DATABASE pixels_console'
            if ($LASTEXITCODE -ne 0) { throw 'Old Console database drop failed; verified backup remains.' }
            & $postgresClient @commonArguments -c `
                'DROP ROLE pixels_console_backup, pixels_console_runtime, pixels_console_owner'
            if ($LASTEXITCODE -ne 0) { throw 'Old Console role drop failed; verified backup remains.' }
            [pscustomobject]@{
                result = 'FRESH_CONSOLE_DATABASE_READY'
                preserved_backup = $archivePath
                archive_sha256 = $archiveHash
            } | ConvertTo-Json -Compress
        } finally {
            Remove-Item Env:PGPASSWORD -ErrorAction SilentlyContinue
            Remove-Item Env:PGSSLMODE -ErrorAction SilentlyContinue
            Remove-Item Env:PGSSLROOTCERT -ErrorAction SilentlyContinue
        }
    }
} finally {
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
