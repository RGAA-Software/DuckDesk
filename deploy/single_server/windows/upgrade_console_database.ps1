#requires -Version 5.1
# Functions are loaded only after the complete package has passed exact SHA-256 verification.

function Protect-UpgradeDirectory {
    param([string]$Path)
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $directoryEntry = Get-Item -LiteralPath $Path -Force
    if (($directoryEntry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Database upgrade directory cannot be a reparse point.'
    }
    $administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $localSystem = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $directorySecurity = [Security.AccessControl.DirectorySecurity]::new()
    $directorySecurity.SetAccessRuleProtection($true, $false)
    $directorySecurity.SetOwner($administrators)
    foreach ($principal in @($administrators, $localSystem)) {
        $directorySecurity.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            $principal, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    }
    [IO.Directory]::SetAccessControl($Path, $directorySecurity)
}

function Invoke-ConsoleDatabasePreUpgradeBackup {
    param([string]$PackageRoot, [string]$ConfigRoot, [string]$DataRoot, [guid]$DeploymentId)
    $backupConfiguration = Get-Content -LiteralPath (Join-Path $ConfigRoot 'backup.json') -Raw | ConvertFrom-Json
    $requiredTargets = @($backupConfiguration.plan.targets | Where-Object { $_.state -eq 'required' })
    if ($requiredTargets.Count -ne 1 -or $requiredTargets[0].database.service -cne 'console' -or
        $requiredTargets[0].database.database -cne 'pixels_console' -or
        [guid]$backupConfiguration.deployment_id -ne $DeploymentId) {
        throw 'Pre-upgrade backup must target this Console database only.'
    }
    $databaseTarget = $requiredTargets[0].database
    $packageManifest = Get-Content -LiteralPath (Join-Path $PackageRoot 'sha256.json') -Raw | ConvertFrom-Json
    foreach ($toolName in @('psql.exe', 'pg_dump.exe', 'pg_restore.exe')) {
        $toolPath = Join-Path $PackageRoot "postgresql/bin/$toolName"
        if ((Get-LowerHash -Path $toolPath) -cne [string]$packageManifest.files."postgresql/bin/$toolName") {
            throw "Pre-upgrade PostgreSQL tool hash differs: $toolName"
        }
    }
    $backupParent = Join-Path $DataRoot 'database-upgrades'
    Protect-UpgradeDirectory -Path $backupParent
    $snapshotRoot = Join-Path $backupParent ([guid]::NewGuid().ToString('N'))
    Protect-UpgradeDirectory -Path $snapshotRoot
    $dumpPath = Join-Path $snapshotRoot 'pixels_console.dump'
    $previousPgPasswordFile = $env:PGPASSFILE
    $previousPgPassword = $env:PGPASSWORD
    $previousPgSslMode = $env:PGSSLMODE
    $previousPgConnectTimeout = $env:PGCONNECT_TIMEOUT
    try {
        $env:PGPASSFILE = [string]$databaseTarget.password_file
        Remove-Item Env:PGPASSWORD -ErrorAction SilentlyContinue
        $env:PGSSLMODE = 'prefer'
        $env:PGCONNECT_TIMEOUT = '5'
        $connectionArguments = @('-h', [string]$databaseTarget.host, '-p', [string]$databaseTarget.port,
            '-U', [string]$databaseTarget.username, '-d', 'pixels_console', '--no-password')
        $queryTool = Join-Path $PackageRoot 'postgresql/bin/psql.exe'
        $inspectionSql = "SELECT deployment_id::text || ':' || service FROM pixels.deployment_identity; SELECT max(version) FROM pixels._sqlx_migrations;"
        $identityOutput = @(& $queryTool @connectionArguments -X -A -t -v ON_ERROR_STOP=1 -c $inspectionSql)
        if ($LASTEXITCODE -ne 0 -or $identityOutput.Count -ne 2 -or
            $identityOutput[0] -cne "$($DeploymentId.ToString()):console" -or $identityOutput[1] -notmatch '^\d+$') {
            throw 'Pre-upgrade database identity or migration ledger differs.'
        }
        & (Join-Path $PackageRoot 'postgresql/bin/pg_dump.exe') @connectionArguments --format custom --file $dumpPath
        if ($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $dumpPath).Length -le 0) {
            throw 'Pre-upgrade Console database backup failed.'
        }
        & (Join-Path $PackageRoot 'postgresql/bin/pg_restore.exe') --list $dumpPath | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Pre-upgrade Console database archive is invalid.' }
        $configSnapshot = Join-Path $snapshotRoot 'config'
        Protect-UpgradeDirectory -Path $configSnapshot
        foreach ($configEntry in Get-ChildItem -LiteralPath $ConfigRoot -Force) {
            Copy-Item -LiteralPath $configEntry.FullName -Destination $configSnapshot -Recurse
        }
        $configurationHashes = @{}
        foreach ($snapshotFile in Get-ChildItem -LiteralPath $configSnapshot -Recurse -File) {
            $relativePath = $snapshotFile.FullName.Substring($configSnapshot.Length + 1).Replace('\', '/')
            $configurationHashes[$relativePath] = Get-LowerHash -Path $snapshotFile.FullName
        }
        $snapshotManifest = @{
            schema_version = 1; deployment_id = $DeploymentId.ToString(); database = 'pixels_console'
            database_schema_version = [long]$identityOutput[1]; archive_sha256 = Get-LowerHash -Path $dumpPath
            archive_bytes = (Get-Item -LiteralPath $dumpPath).Length; created_at_utc = [datetime]::UtcNow.ToString('o')
            configuration_sha256 = $configurationHashes
        } | ConvertTo-Json -Depth 5
        [IO.File]::WriteAllText((Join-Path $snapshotRoot 'backup-manifest.json'), $snapshotManifest, [Text.UTF8Encoding]::new($false))
        Write-Output "PRE_UPGRADE_BACKUP=$snapshotRoot"
    } finally {
        $env:PGPASSFILE = $previousPgPasswordFile
        $env:PGPASSWORD = $previousPgPassword
        $env:PGSSLMODE = $previousPgSslMode
        $env:PGCONNECT_TIMEOUT = $previousPgConnectTimeout
    }
}

function Set-ConsoleBackupSchemaVersion {
    param([string]$ConfigRoot, [long]$SchemaVersion)
    if ($SchemaVersion -le 0) { throw 'Backup schema version is invalid.' }
    $backupPath = Join-Path $ConfigRoot 'backup.json'
    $backupConfiguration = Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
    $consoleTargets = @($backupConfiguration.plan.targets | Where-Object {
        $_.state -eq 'required' -and $_.database.service -eq 'console'
    })
    if ($consoleTargets.Count -ne 1) { throw 'Console Backup target is not unique.' }
    $consoleTargets[0].database.schema_version = $SchemaVersion
    $temporaryPath = Join-Path $ConfigRoot ('.backup-schema-' + [guid]::NewGuid().ToString('N'))
    try {
        [IO.File]::WriteAllText($temporaryPath, ($backupConfiguration | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
        [IO.File]::SetAccessControl($temporaryPath, [IO.File]::GetAccessControl($backupPath))
        [IO.File]::Replace($temporaryPath, $backupPath, [NullString]::Value)
    } finally {
        if (Test-Path -LiteralPath $temporaryPath) { Remove-Item -LiteralPath $temporaryPath }
    }
}

function Remove-ObsoleteGuestLifetimeSetting {
    param([string]$ConfigRoot)
    $environmentPath = Join-Path $ConfigRoot 'console.env'
    $previousEnvironment = [IO.File]::ReadAllText($environmentPath)
    $nextEnvironment = [regex]::Replace($previousEnvironment,
        '(?m)^PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS=[^\r\n]*(?:\r?\n|$)', '')
    if ($nextEnvironment -ceq $previousEnvironment) { return }
    $temporaryPath = Join-Path $ConfigRoot ('.guest-lifetime-retired-' + [guid]::NewGuid().ToString('N'))
    try {
        [IO.File]::WriteAllText($temporaryPath, $nextEnvironment, [Text.UTF8Encoding]::new($false))
        [IO.File]::SetAccessControl($temporaryPath, [IO.File]::GetAccessControl($environmentPath))
        [IO.File]::Replace($temporaryPath, $environmentPath, [NullString]::Value)
    } finally {
        if (Test-Path -LiteralPath $temporaryPath) { Remove-Item -LiteralPath $temporaryPath }
    }
}
