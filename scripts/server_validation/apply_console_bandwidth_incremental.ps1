#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$BundleRoot,[switch]$VerifyOnly)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$bundleDirectory = [IO.Path]::GetFullPath($BundleRoot)
$plan = Get-Content -LiteralPath (Join-Path $bundleDirectory 'update-plan.json') -Raw | ConvertFrom-Json
if ($plan.service_name -ne 'Pixels.Console' -or $plan.manifest_name -ne 'sha256.json' -or
    $plan.source_schema_version -ne 41 -or $plan.target_schema_version -ne 42) { throw 'Expected Console schema41 to schema42 update' }
$configDirectory = 'C:\ProgramData\Pixels\Server\config'
$backupPath = Join-Path $configDirectory 'backup.json'
if ((Get-FileHash -LiteralPath $backupPath).Hash -ine $plan.baseline_backup_config_sha256) { throw 'Backup configuration changed after preflight' }
$backupConfiguration = Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
$consoleTargets = @($backupConfiguration.plan.targets | Where-Object {$_.database.database -eq 'pixels_console'})
if ($consoleTargets.Count -ne 1 -or $consoleTargets[0].database.schema_version -ne 41) { throw 'Expected one Console backup target at schema41' }
$schemaAdministrator = Join-Path $bundleDirectory 'payload\bin\px_console_admin.exe'
$administratorReplacement = @($plan.replacements | Where-Object {$_.path -eq 'bin/px_console_admin.exe'})
if ($administratorReplacement.Count -ne 1 -or (Get-FileHash -LiteralPath $schemaAdministrator).Hash -ine $administratorReplacement[0].after_sha256) {
    throw 'Schema administrator identity differs'
}
$desiredManifest = Get-Content -LiteralPath (Join-Path $bundleDirectory 'desired-manifest.json') -Raw | ConvertFrom-Json
if (-not $desiredManifest.files.'static/console/index.html') { throw 'Installed Console web entry point missing' }
& (Join-Path $bundleDirectory 'apply.ps1') -BundleRoot $bundleDirectory -VerifyOnly
& $schemaAdministrator preflight-single-server-database $configDirectory
if ($LASTEXITCODE -ne 0) { throw 'Console schema preflight failed' }
if ($VerifyOnly) { return }
$backupServices = @(Get-Service | Where-Object {$_.Name -like 'Pixels.Backup.*'})
if ($backupServices.Count -ne 1) { throw 'Backup service identity is ambiguous' }
$backupServiceName = $backupServices[0].Name
$snapshotDirectory = Join-Path $bundleDirectory 'database-before-schema42'
if (Test-Path -LiteralPath $snapshotDirectory) { throw 'Preserve the existing database snapshot' }
[void][IO.Directory]::CreateDirectory($snapshotDirectory)
& icacls.exe $snapshotDirectory '/inheritance:r' '/grant:r' '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' *> $null
if ($LASTEXITCODE -ne 0) { throw 'Private database snapshot ACL failed' }
Copy-Item -LiteralPath $backupPath -Destination (Join-Path $snapshotDirectory 'backup.json')
$backupStopped = $false
$consoleStopped = $false
try {
    Stop-Service -Name $backupServiceName
    (Get-Service -Name $backupServiceName).WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(30))
    $backupStopped = $true
    Stop-Service -Name Pixels.Console
    (Get-Service -Name Pixels.Console).WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(30))
    $consoleStopped = $true
    $ownerCredentialPath = Join-Path $configDirectory 'database-upgrade\console-owner.url'
    $ownerEndpoint = [Uri]([IO.File]::ReadAllText($ownerCredentialPath).Trim())
    $ownerCredentials = $ownerEndpoint.UserInfo.Split(':',2)
    if ($ownerCredentials.Count -ne 2 -or $ownerCredentials[0] -ne 'pixels_console_owner' -or $ownerEndpoint.AbsolutePath -ne '/pixels_console') {
        throw 'Unexpected offline Console owner identity'
    }
    $dumpExecutable = Join-Path $plan.installed_root 'postgresql\bin\pg_dump.exe'
    $restoreExecutable = Join-Path $plan.installed_root 'postgresql\bin\pg_restore.exe'
    $baseline = Get-Content -LiteralPath (Join-Path $plan.installed_root 'sha256.json') -Raw | ConvertFrom-Json
    foreach ($toolIdentity in @(@{path=$dumpExecutable;relative='postgresql/bin/pg_dump.exe'},@{path=$restoreExecutable;relative='postgresql/bin/pg_restore.exe'})) {
        if ((Get-FileHash -LiteralPath $toolIdentity.path).Hash -ine $baseline.files.($toolIdentity.relative)) { throw 'PostgreSQL tool identity differs' }
    }
    $dumpPath = Join-Path $snapshotDirectory 'console.dump'
    $dumpOptions = [Diagnostics.ProcessStartInfo]::new()
    $dumpOptions.FileName = $dumpExecutable
    $dumpOptions.UseShellExecute = $false
    $dumpOptions.CreateNoWindow = $true
    $dumpOptions.Arguments = '--format=custom --host=' + $ownerEndpoint.Host + ' --port=' + $ownerEndpoint.Port +
        ' --username=pixels_console_owner --no-password --file="' + $dumpPath + '" pixels_console'
    $dumpOptions.EnvironmentVariables['PGPASSWORD'] = [Uri]::UnescapeDataString($ownerCredentials[1])
    $dumpOptions.EnvironmentVariables['PGSSLMODE'] = 'prefer'
    $dumpProcess = [Diagnostics.Process]::new()
    $dumpProcess.StartInfo = $dumpOptions
    try {
        if (-not $dumpProcess.Start()) { throw 'Database snapshot did not start' }
        if (-not $dumpProcess.WaitForExit(60000)) { $dumpProcess.Kill(); throw 'Database snapshot timed out' }
        if ($dumpProcess.ExitCode -ne 0) { throw 'Database snapshot failed' }
    } finally {
        $dumpProcess.Dispose()
        $dumpOptions.EnvironmentVariables.Remove('PGPASSWORD')
        $ownerEndpoint = $null
        $ownerCredentials = $null
    }
    & $restoreExecutable --list $dumpPath *> (Join-Path $snapshotDirectory 'dump-list.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Database snapshot archive verification failed' }
    & $schemaAdministrator upgrade-single-server-database $configDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Console migration failed; files retained, do not retry automatically' }
    $consoleTargets[0].database.schema_version = 42
    [IO.File]::WriteAllText($backupPath,($backupConfiguration | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
    & (Join-Path $bundleDirectory 'apply.ps1') -BundleRoot $bundleDirectory
    $consoleStopped = $false
    Start-Service -Name $backupServiceName
    (Get-Service -Name $backupServiceName).WaitForStatus([ServiceProcess.ServiceControllerStatus]::Running,[TimeSpan]::FromSeconds(30))
    $backupStopped = $false
    [ordered]@{schema=42;database_snapshot_sha256=(Get-FileHash -LiteralPath $dumpPath).Hash;
        backup_config_sha256=(Get-FileHash -LiteralPath $backupPath).Hash;backup_service_state=(Get-Service -Name $backupServiceName).Status.ToString()} | ConvertTo-Json -Compress
} catch {
    # A migration may already have committed. Preserve evidence and prefer the
    # reviewed new runtime when its files were published; never reverse schema here.
    if ($consoleStopped) { Start-Service -Name Pixels.Console -ErrorAction Continue }
    if ($backupStopped) { Start-Service -Name $backupServiceName -ErrorAction Continue }
    throw
}
