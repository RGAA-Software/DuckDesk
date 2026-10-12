#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$BundleRoot, [switch]$VerifyOnly)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$bundleDirectory = [IO.Path]::GetFullPath($BundleRoot)
$plan = Get-Content -LiteralPath (Join-Path $bundleDirectory 'update-plan.json') -Raw | ConvertFrom-Json
if ($plan.service_name -ne 'Pixels.Console' -or $plan.manifest_name -ne 'sha256.json') {
    throw 'RDP pool migration requires the installed Pixels Console bundle'
}
$desiredManifest = Get-Content -LiteralPath (Join-Path $bundleDirectory 'desired-manifest.json') -Raw | ConvertFrom-Json
if (-not $desiredManifest.files.'static/console/index.html' -or
    @($plan.removals | Where-Object {$_.path -eq 'static/console/index.html'}).Count -ne 0) {
    throw 'Console update must retain the installed static/console web entry point'
}
$configDirectory = 'C:\ProgramData\Pixels\Server\config'
$schemaAdministrator = Join-Path $bundleDirectory 'payload\bin\px_console_admin.exe'
$administratorReplacement = @($plan.replacements | Where-Object {$_.path -eq 'bin/px_console_admin.exe'})
if ($administratorReplacement.Count -ne 1 -or
    (Get-FileHash -LiteralPath $schemaAdministrator).Hash -ine $administratorReplacement[0].after_sha256) {
    throw 'Schema administrator identity differs from the reviewed bundle'
}
$backupPath = Join-Path $configDirectory 'backup.json'
if ((Get-FileHash -LiteralPath $backupPath).Hash -ine $plan.baseline_backup_config_sha256) {
    throw 'Backup configuration changed after preflight'
}
$backupConfiguration = Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
$consoleTargets = @($backupConfiguration.plan.targets | Where-Object {$_.database.database -eq 'pixels_console'})
if ($consoleTargets.Count -ne 1 -or $consoleTargets[0].database.schema_version -ne 40) {
    throw 'Expected exactly one Console backup target at schema 40'
}
& $schemaAdministrator preflight-single-server-database $configDirectory
if ($LASTEXITCODE -ne 0) { throw 'Console schema preflight failed' }
& (Join-Path $bundleDirectory 'apply.ps1') -BundleRoot $bundleDirectory -VerifyOnly
if ($VerifyOnly) { return }

$backupServices = @(Get-Service | Where-Object {$_.Name -like 'Pixels.Backup.*'})
if ($backupServices.Count -ne 1) { throw 'Backup service identity is ambiguous' }
$backupServiceName = $backupServices[0].Name
$savedBackupPath = Join-Path $bundleDirectory 'backup-config-before-schema41.json'
if (Test-Path -LiteralPath $savedBackupPath) { throw 'Preserve the prior schema upgrade configuration backup' }
Copy-Item -LiteralPath $backupPath -Destination $savedBackupPath
& icacls.exe $savedBackupPath '/inheritance:r' '/grant:r' '*S-1-5-18:F' '*S-1-5-32-544:F' *> $null
if ($LASTEXITCODE -ne 0) { throw 'Private backup configuration ACL failed' }

Stop-Service -Name $backupServiceName
(Get-Service -Name $backupServiceName).WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(30))
Stop-Service -Name Pixels.Console
(Get-Service -Name Pixels.Console).WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(30))
& $schemaAdministrator upgrade-single-server-database $configDirectory
if ($LASTEXITCODE -ne 0) {
    # SQLx applies each migration transactionally. A failed upgrade cannot start
    # this new runtime; retain the existing files and restore their services.
    Start-Service -Name Pixels.Console
    Start-Service -Name $backupServiceName
    throw 'Console schema migration failed; existing service files retained'
}
$consoleTargets[0].database.schema_version = 41
[IO.File]::WriteAllText($backupPath,($backupConfiguration | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
& (Join-Path $bundleDirectory 'apply.ps1') -BundleRoot $bundleDirectory
Start-Service -Name $backupServiceName
(Get-Service -Name $backupServiceName).WaitForStatus([ServiceProcess.ServiceControllerStatus]::Running,[TimeSpan]::FromSeconds(30))
[ordered]@{schema=41;backup_service=$backupServiceName;backup_service_state=(Get-Service -Name $backupServiceName).Status.ToString();
    backup_config_sha256=(Get-FileHash -LiteralPath $backupPath).Hash;backup_config_before=$savedBackupPath} | ConvertTo-Json -Compress
