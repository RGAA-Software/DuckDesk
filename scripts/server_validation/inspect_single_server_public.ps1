#requires -Version 5.1

[CmdletBinding()]
param([string]$ComputerName = '39.71.45.66')

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$machineFile = Join-Path $repositoryRoot '.env/test_machine.md'
$machineText = Get-Content -LiteralPath $machineFile -Raw -Encoding UTF8
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
        $serviceRecords = @(Get-CimInstance Win32_Service | Where-Object {
            $_.Name -like 'Pixels*' -or $_.Name -like 'postgresql*'
        } | Select-Object Name, State, StartMode, StartName, PathName)
        $installationPaths = @(
            'D:\PixelsServer',
            'C:\Program Files\Pixels\Server',
            'C:\ProgramData\Pixels\Server'
        )
        $installationRecords = @($installationPaths | ForEach-Object {
            [pscustomobject]@{
                path = $_
                exists = Test-Path -LiteralPath $_
                has_console = Test-Path -LiteralPath (Join-Path $_ 'current\bin\px_console.exe')
                has_setup_marker = Test-Path -LiteralPath (Join-Path $_ 'setup-complete.json')
            }
        })
        $portRecords = @(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | Where-Object {
            $_.LocalPort -in @(4600, 4605, 4700, 5432, 54329)
        } | Select-Object LocalAddress, LocalPort, OwningProcess)
        $legacyRoot = 'D:\PixelsServer'
        $legacyDirectories = if (Test-Path -LiteralPath $legacyRoot) {
            @(Get-ChildItem -LiteralPath $legacyRoot -Directory | Select-Object -ExpandProperty Name)
        } else { @() }
        $scheduledTasks = @(Get-ScheduledTask | Where-Object {
            $_.TaskName -like '*Pixels*' -or $_.TaskPath -like '*Pixels*'
        } | Select-Object TaskName, TaskPath, State)
        $legacyFileNames = @{}
        foreach ($subdirectory in @('config', 'private', 'secrets')) {
            $subdirectoryPath = Join-Path $legacyRoot $subdirectory
            $legacyFileNames[$subdirectory] = if (Test-Path -LiteralPath $subdirectoryPath) {
                @(Get-ChildItem -LiteralPath $subdirectoryPath -File | Select-Object -ExpandProperty Name)
            } else { @() }
        }
        $postgresConfiguration = 'D:\PixelsServer\data\postgresql\postgresql.conf'
        $postgresHostRules = 'D:\PixelsServer\data\postgresql\pg_hba.conf'
        $postgresSettings = if (Test-Path -LiteralPath $postgresConfiguration) {
            @(Get-Content -LiteralPath $postgresConfiguration | Where-Object {
                $_ -match '^\s*(ssl|ssl_cert_file|ssl_key_file|listen_addresses|port)\s*='
            } | ForEach-Object { [string]$_ })
        } else { @() }
        $postgresRules = if (Test-Path -LiteralPath $postgresHostRules) {
            @(Get-Content -LiteralPath $postgresHostRules | Where-Object {
                $_ -match '^\s*host' -and $_ -notmatch '^\s*#'
            } | ForEach-Object { [string]$_ })
        } else { @() }
        $postgresInventory = $null
        if ((Get-Service -Name PixelsPostgreSQL18).Status -eq 'Running') {
            $postgresClient = 'D:\PixelsServer\postgresql\pgsql\bin\psql.exe'
            try {
                $env:PGPASSWORD = (Get-Content -LiteralPath 'D:\PixelsServer\config\pg-admin-password' -Raw).Trim()
                $env:PGSSLMODE = 'verify-full'
                $env:PGSSLROOTCERT = 'D:\PixelsServer\app\tls\console-ca.pem'
                $databaseNames = @(& $postgresClient -X -A -t -h localhost -p 54329 -U postgres -d postgres `
                    -c 'SELECT datname FROM pg_database WHERE datistemplate = false ORDER BY datname')
                $consoleRoles = @(& $postgresClient -X -A -t -h localhost -p 54329 -U postgres -d postgres `
                    -c "SELECT rolname FROM pg_roles WHERE rolname LIKE 'pixels_console_%' ORDER BY rolname")
                $consoleConnections = @(& $postgresClient -X -A -t -h localhost -p 54329 -U postgres -d postgres `
                    -c "SELECT count(*) FROM pg_stat_activity WHERE datname = 'pixels_console'")
                $consoleDatabaseSize = @(& $postgresClient -X -A -t -h localhost -p 54329 -U postgres -d postgres `
                    -c "SELECT CASE WHEN EXISTS (SELECT 1 FROM pg_database WHERE datname='pixels_console') THEN pg_database_size('pixels_console') ELSE 0 END")
                if ($LASTEXITCODE -ne 0) { throw 'PostgreSQL inventory query failed.' }
                $postgresInventory = [pscustomobject]@{
                    databases = $databaseNames
                    console_roles = $consoleRoles
                    console_connections = $consoleConnections
                    console_database_bytes = $consoleDatabaseSize
                }
            } finally {
                Remove-Item Env:PGPASSWORD -ErrorAction SilentlyContinue
                Remove-Item Env:PGSSLMODE -ErrorAction SilentlyContinue
                Remove-Item Env:PGSSLROOTCERT -ErrorAction SilentlyContinue
            }
        }
        $postgresProcess = @(Get-CimInstance Win32_Process | Where-Object {
            $_.ProcessId -in @($portRecords | Where-Object LocalPort -eq 54329 | Select-Object -ExpandProperty OwningProcess)
        } | Select-Object ProcessId, CommandLine)
        $postgresEvents = @(Get-WinEvent -FilterHashtable @{
            LogName = 'Application'
            StartTime = (Get-Date).AddMinutes(-15)
        } -ErrorAction SilentlyContinue | Where-Object {
            $_.ProviderName -match 'postgres|Pixels' -or $_.Message -match 'PixelsPostgreSQL18|postgresql'
        } | Select-Object -First 6 | ForEach-Object {
            [pscustomobject]@{
                time = $_.TimeCreated.ToUniversalTime().ToString('o')
                provider = $_.ProviderName
                message = ([string]$_.Message).Substring(0, [Math]::Min(500, ([string]$_.Message).Length))
            }
        })
        $tlsPaths = @(
            'D:\PixelsServer\app\tls\console.crt',
            'D:\PixelsServer\app\tls\console.key',
            'D:\PixelsServer\retired-runtime-20260926\app\tls\console.crt',
            'D:\PixelsServer\retired-runtime-20260926\app\tls\console.key',
            'D:\PixelsServer\retired-runtime-20260926\app\tls\console-ca.pem'
        )
        $tlsRecords = @($tlsPaths | ForEach-Object {
            $certificateFile = Get-Item -LiteralPath $_ -ErrorAction SilentlyContinue
            [pscustomobject]@{
                path = $_
                exists = [bool]$certificateFile
                last_write = if ($certificateFile) { $certificateFile.LastWriteTimeUtc.ToString('o') } else { $null }
            }
        })
        $retiredTlsPath = 'D:\PixelsServer\retired-runtime-20260926\app\tls'
        $retiredTlsNames = if (Test-Path -LiteralPath $retiredTlsPath) {
            @(Get-ChildItem -LiteralPath $retiredTlsPath -File | Select-Object -ExpandProperty Name)
        } else { @() }
        $tlsAclRecords = @('D:\PixelsServer\app', 'D:\PixelsServer\app\tls',
            'D:\PixelsServer\app\tls\console.key',
            'D:\PixelsServer\retired-runtime-20260926\app\tls\console.key') | ForEach-Object {
            $fileAcl = Get-Acl -LiteralPath $_ -ErrorAction SilentlyContinue
            [pscustomobject]@{
                path = $_
                owner = if ($fileAcl) { [string]$fileAcl.Owner } else { $null }
                grants = if ($fileAcl) { @($fileAcl.Access | ForEach-Object {
                    "{0}:{1}" -f $_.IdentityReference, $_.FileSystemRights
                }) } else { @() }
            }
        }
        $postgresLogDirectory = 'D:\PixelsServer\data\postgresql\log'
        $newestPostgresLog = if (Test-Path -LiteralPath $postgresLogDirectory) {
            Get-ChildItem -LiteralPath $postgresLogDirectory -File | Sort-Object LastWriteTimeUtc -Descending |
                Select-Object -First 1
        } else { $null }
        $recentPostgresLog = if ($newestPostgresLog) {
            @(Get-Content -LiteralPath $newestPostgresLog.FullName -Tail 12 | ForEach-Object { [string]$_ })
        } else { @() }
        $postgresCertificate = if (Test-Path -LiteralPath (Join-Path $retiredTlsPath 'console.crt')) {
            $certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2(
                (Join-Path $retiredTlsPath 'console.crt'))
            [pscustomobject]@{
                subject = $certificate.Subject
                not_after = $certificate.NotAfter.ToUniversalTime().ToString('o')
                san = @($certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.17' } |
                    ForEach-Object { $_.Format($false) })
            }
        } else { $null }
        $backupStatusPath = 'C:\ProgramData\Pixels\Server\data\backup\status\status.json'
        $backupStatus = if (Test-Path -LiteralPath $backupStatusPath -PathType Leaf) {
            $statusRecord = Get-Content -LiteralPath $backupStatusPath -Raw | ConvertFrom-Json
            [pscustomobject]@{
                last_recovery_set_id = $statusRecord.last_recovery_set_id
                last_success_at_unix = $statusRecord.last_success_at_unix
                consecutive_failures = $statusRecord.consecutive_failures
                alerts = $statusRecord.alerts
            }
        } else { $null }
        [pscustomobject]@{
            host = $env:COMPUTERNAME
            installed_suite_version = (Get-ItemProperty -LiteralPath `
                'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer').DisplayVersion
            services = $serviceRecords
            paths = $installationRecords
            listeners = $portRecords
            legacy_directories = $legacyDirectories
            legacy_file_names = $legacyFileNames
            postgres_settings = $postgresSettings
            postgres_rules = $postgresRules
            postgres_inventory = $postgresInventory
            postgres_process = $postgresProcess
            postgres_events = $postgresEvents
            tls_paths = $tlsRecords
            retired_tls_names = $retiredTlsNames
            tls_acls = $tlsAclRecords
            postgres_log = $recentPostgresLog
            postgres_certificate = $postgresCertificate
            backup_status = $backupStatus
            scheduled_tasks = $scheduledTasks
        } | ConvertTo-Json -Depth 5 -Compress
    }
} finally {
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
