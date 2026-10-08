#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [guid]$RecoverySetId,
    [string]$PgPasswordFile = '',
    [string]$PgUser = 'postgres',
    [string]$ConfigRoot = "$env:ProgramData\Pixels\Server\config",
    [string]$InstallRoot = "$env:ProgramFiles\Pixels\Server",
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'

function Get-FileHashLower {
    param([string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $hasher.Dispose()
        $stream.Dispose()
    }
}

function Assert-PlainFile {
    param([string]$Path)
    $fileEntry = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($fileEntry.PSIsContainer -or ($fileEntry.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Expected a regular file: $Path"
    }
}

function Invoke-PostgresTool {
    param([string]$Path, [string[]]$Arguments)
    & $Path @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$([IO.Path]::GetFileName($Path)) failed with exit code $LASTEXITCODE" }
}

if ($RecoverySetId -eq [guid]::Empty) { throw 'A nonempty recovery set ID is required.' }
if ($PgUser -cnotmatch '^[a-z_][a-z0-9_]*$') { throw 'PostgreSQL administrator role is invalid.' }
$configPath = Join-Path ([IO.Path]::GetFullPath($ConfigRoot)) 'backup.json'
Assert-PlainFile $configPath
$backupConfig = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if ($backupConfig.schema_version -ne 2 -or [guid]$backupConfig.deployment_id -eq [guid]::Empty -or
    $backupConfig.plan.kind -cne 'independent' -or $backupConfig.plan.deployment_id -cne $backupConfig.deployment_id) {
    throw 'Single Server Backup configuration differs.'
}
$databaseTarget = @($backupConfig.plan.targets | Where-Object { $_.service -eq 'console' -and $_.state -eq 'required' })
if ($databaseTarget.Count -ne 1 -or $databaseTarget[0].database.database -cne 'pixels_console' -or
    @($backupConfig.plan.targets).Count -ne 3 -or
    @($backupConfig.plan.targets | Where-Object { $_.service -eq 'auth' -and $_.state -eq 'not_applicable' }).Count -ne 1 -or
    @($backupConfig.plan.targets | Where-Object { $_.service -eq 'desk' -and $_.state -eq 'not_applicable' }).Count -ne 1) {
    throw 'Backup does not contain exactly one Console database target.'
}
$repositoryRoot = [IO.Path]::GetFullPath([string]$backupConfig.repository_root)
$setDirectory = Join-Path $repositoryRoot $RecoverySetId.ToString()
$manifestPath = Join-Path $setDirectory 'manifest.json'
$archivePath = Join-Path $setDirectory 'console.dump'
Assert-PlainFile $manifestPath
Assert-PlainFile $archivePath
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema_version -ne 3 -or $manifest.recovery_set_id -cne $RecoverySetId.ToString() -or
    $manifest.deployment_id -cne $backupConfig.deployment_id -or $manifest.kind -cne 'independent' -or
    $manifest.status -cnotin @('verified', 'offsite_verified', 'restore_tested') -or
    @($manifest.members).Count -ne 3) { throw 'Recovery set identity or state differs.' }
$consoleMember = @($manifest.members | Where-Object { $_.service -eq 'console' -and $_.member.state -eq 'required' })
$otherMembers = @($manifest.members | Where-Object { $_.service -in @('auth', 'desk') -and $_.member.state -eq 'not_applicable' })
if ($consoleMember.Count -ne 1 -or $otherMembers.Count -ne 2 -or
    @($otherMembers | Where-Object { $_.service -eq 'auth' }).Count -ne 1 -or
    @($otherMembers | Where-Object { $_.service -eq 'desk' }).Count -ne 1 -or
    $consoleMember[0].member.database -cne 'pixels_console' -or
    $consoleMember[0].member.archive_file -cne 'console.dump' -or
    $manifest.security_evidence.state -cne 'unavailable' -or
    $manifest.security_evidence.reason -cne 'independent_backup') {
    throw 'Recovery set is not a Console-only Single Server backup.'
}
$expectedSchemaVersion = [int]$consoleMember[0].member.schema_version
if ($expectedSchemaVersion -le 0) { throw 'Console schema version is invalid.' }
$expectedHash = [string]$consoleMember[0].member.archive_sha256
if ($expectedHash -cnotmatch '^[0-9a-f]{64}$' -or (Get-FileHashLower $archivePath) -cne $expectedHash) {
    throw 'Console archive SHA-256 differs from its manifest.'
}
$toolRoot = Join-Path ([IO.Path]::GetFullPath($InstallRoot)) 'current\postgresql\bin'
$createDatabaseTool = Join-Path $toolRoot 'createdb.exe'
$restoreTool = Join-Path $toolRoot 'pg_restore.exe'
$queryTool = Join-Path $toolRoot 'psql.exe'
foreach ($toolPath in @($createDatabaseTool, $restoreTool, $queryTool)) { Assert-PlainFile $toolPath }
$packageManifestPath = Join-Path ([IO.Path]::GetFullPath($InstallRoot)) 'current\sha256.json'
Assert-PlainFile $packageManifestPath
$packageManifest = Get-Content -LiteralPath $packageManifestPath -Raw | ConvertFrom-Json
foreach ($toolName in @('createdb.exe', 'pg_restore.exe', 'psql.exe')) {
    $expectedToolHash = [string]$packageManifest.files."postgresql/bin/$toolName"
    if ($expectedToolHash -cnotmatch '^[0-9a-f]{64}$' -or
        (Get-FileHashLower (Join-Path $toolRoot $toolName)) -cne $expectedToolHash) {
        throw "Packaged PostgreSQL tool SHA-256 differs: $toolName"
    }
}
$targetDatabase = "pixels_console_restore_$($RecoverySetId.ToString('N'))"
Write-Output "Verified Console backup $RecoverySetId; isolated target: $targetDatabase"
if (-not $Execute) {
    Write-Output 'Preflight only. Add -Execute to create and restore the isolated database; the live Console database will not change.'
    return
}
if (-not $PgPasswordFile) { throw '-PgPasswordFile is required with -Execute.' }
$passwordPath = [IO.Path]::GetFullPath($PgPasswordFile)
Assert-PlainFile $passwordPath

$statusPath = Join-Path ([string]$backupConfig.status_root) 'status.json'
Assert-PlainFile $statusPath
$status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
if ($status.deployment_id -cne $backupConfig.deployment_id -or $null -ne $status.active_task) {
    throw 'Backup is active or its status differs; retry after it becomes idle.'
}
$backupServiceName = "Pixels.Backup.$(([guid]$backupConfig.deployment_id).ToString('N').Substring(0, 12))"
$backupService = Get-Service -Name $backupServiceName -ErrorAction Stop
$backupWasRunning = $backupService.Status -eq 'Running'
$previousPgpass = $env:PGPASSFILE
$previousSslMode = $env:PGSSLMODE
try {
    if ($backupWasRunning) { Stop-Service -Name $backupServiceName -ErrorAction Stop }
    $env:PGPASSFILE = $passwordPath
    $env:PGSSLMODE = 'prefer'
    $hostName = [string]$databaseTarget[0].database.host
    $port = [string]$databaseTarget[0].database.port
    $connectionArguments = @('--host', $hostName, '--port', $port, '--username', $PgUser, '--no-password')
    Invoke-PostgresTool $createDatabaseTool ($connectionArguments + @('--maintenance-db=postgres', '--template=template0', '--owner=pixels_console_owner', $targetDatabase))
    $databaseGrants = "REVOKE ALL ON DATABASE $targetDatabase FROM PUBLIC; GRANT CONNECT ON DATABASE $targetDatabase TO pixels_console_runtime"
    Invoke-PostgresTool $queryTool ($connectionArguments + @('-X', '--set=ON_ERROR_STOP=1', '--dbname', 'postgres', '--command', $databaseGrants))
    Invoke-PostgresTool $restoreTool ($connectionArguments + @('--exit-on-error', '--no-owner', '--role=pixels_console_owner', '--dbname', $targetDatabase, $archivePath))
    $identityQuery = "SELECT identity.service || '|' || identity.deployment_id::text || '|' || pg_catalog.pg_get_userbyid(database_record.datdba) || '|' || COUNT(migration.version)::text || '|' || COALESCE(BOOL_AND(migration.success), FALSE)::text FROM pixels.deployment_identity AS identity CROSS JOIN pg_catalog.pg_database AS database_record LEFT JOIN pixels._sqlx_migrations AS migration ON TRUE WHERE database_record.datname = current_database() GROUP BY identity.service, identity.deployment_id, database_record.datdba"
    $identity = (& $queryTool @connectionArguments '-X' '-A' '-t' '--set=ON_ERROR_STOP=1' '--dbname' $targetDatabase '--command' $identityQuery | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $identity -cne "console|$($backupConfig.deployment_id)|pixels_console_owner|$expectedSchemaVersion|true") {
        throw 'Restored Console identity, owner or migration state differs; isolated database retained for inspection.'
    }
    Write-Output "Restored and verified $targetDatabase. The live pixels_console database and Console configuration were not changed."
} finally {
    $env:PGPASSFILE = $previousPgpass
    $env:PGSSLMODE = $previousSslMode
    if ($backupWasRunning) { Start-Service -Name $backupServiceName -ErrorAction Stop }
}
