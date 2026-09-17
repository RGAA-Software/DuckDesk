#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PostgreSqlArchive
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dockerBin = 'C:\Program Files\Docker\Docker\resources\bin'
if (Test-Path -LiteralPath $dockerBin) { $env:Path = "$dockerBin;$env:Path" }
$runId = "windows-backup-$((Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss'))-$([Guid]::NewGuid().ToString('N').Substring(0, 8))"
$testRoot = Join-Path $repositoryRoot ".cache/$runId"
$reportRoot = Join-Path $repositoryRoot "test-results/$runId"
$packageOutputRoot = Join-Path $testRoot 'packages'
$installationRoot = Join-Path $testRoot 'installed'
$dataRoot = Join-Path $testRoot 'data'
$runtimeRoot = Join-Path $testRoot 'runtime'
$containerName = "pixels-$runId"
$deploymentId = [Guid]::NewGuid()
$serviceName = "Pixels.Backup.$($deploymentId.ToString('N').Substring(0, 12))"
$password = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(24)).ToLowerInvariant()
$steps = [Collections.Generic.List[object]]::new()
$containerStarted = $false

function Add-Step {
    param([string]$Name)
    $steps.Add([ordered]@{ case = $Name; status = 'PASS' })
    Write-Output "PASS $Name"
}

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments, [switch]$ExpectFailure)
    $output = & $Executable @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if (($ExpectFailure -and $exitCode -eq 0) -or (-not $ExpectFailure -and $exitCode -ne 0)) {
        throw "Unexpected command result: $Executable exit=$exitCode output=$($output -join ' ')"
    }
    return $output
}

function New-PrivateDirectory {
    param([string]$Path)
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $identity = (& whoami.exe).Trim()
    Invoke-Checked 'icacls.exe' @($Path, '/inheritance:r', '/grant:r', "$identity`:(OI)(CI)F", '*S-1-5-18:(OI)(CI)F') | Out-Null
}

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$postgresPort = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
$listener.Stop()
New-Item -ItemType Directory -Path $testRoot, $reportRoot -Force | Out-Null

try {
    $administrator = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $administrator.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Windows backup package validation requires an elevated PowerShell process.'
    }
    $resolvedArchive = (Resolve-Path -LiteralPath $PostgreSqlArchive).Path
    $backupBinary = Join-Path $repositoryRoot 'rust_server/target/release/px_backup.exe'
    Invoke-Checked 'cargo.exe' @('build', '--offline', '--locked', '--release', '--manifest-path',
        (Join-Path $repositoryRoot 'rust_server/Cargo.toml'), '-p', 'px_backup') | Out-Null
    Add-Step 'BUILD: release px_backup compiled from locked Rust workspace'

    $packageOutput = Invoke-Checked 'pwsh.exe' @('-NoProfile', '-File',
        (Join-Path $repositoryRoot 'scripts/server_backup/build_windows_package.ps1'),
        '-BackupBinary', $backupBinary, '-PostgreSqlArchive', $resolvedArchive, '-OutputRoot', $packageOutputRoot)
    $packagePath = ($packageOutput | Where-Object { $_ -like 'PACKAGE *' }) -replace '^PACKAGE ', ''
    $manifestHash = ($packageOutput | Where-Object { $_ -like 'PACKAGE_MANIFEST_SHA256 *' }) -replace '^PACKAGE_MANIFEST_SHA256 ', ''
    if (-not [IO.Directory]::Exists($packagePath) -or $manifestHash -cnotmatch '^[0-9a-f]{64}$') {
        throw 'Package builder did not publish a verifiable release.'
    }
    Add-Step 'PACKAGE: official PostgreSQL archive and every minimal runtime file matched pinned SHA-256'

    Invoke-Checked 'docker.exe' @('run', '--detach', '--name', $containerName,
        '--label', "pixels.validation=$runId", '-e', "POSTGRES_PASSWORD=$password",
        '-p', "127.0.0.1:$postgresPort`:5432",
        'postgres:18.6@sha256:4ef4dbc939d61acea57712655ddb4b4ab27419c913f94cca0cd57cb3ea3c2280') | Out-Null
    $containerStarted = $true
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    do {
        $readyOutput = & docker.exe exec $containerName pg_isready -h 127.0.0.1 -U postgres 2>&1
        if ($LASTEXITCODE -eq 0) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw "PostgreSQL container did not become ready: $($readyOutput -join ' ')" }
        Start-Sleep -Milliseconds 500
    } while ($true)
    Invoke-Checked 'docker.exe' @('exec', '-e', "PGPASSWORD=$password", $containerName, 'psql', '-X', '-v', 'ON_ERROR_STOP=1',
        '-U', 'postgres', '-d', 'postgres', '-c', 'CREATE TABLE package_probe(id integer PRIMARY KEY); INSERT INTO package_probe VALUES (1);') | Out-Null

    New-PrivateDirectory -Path $runtimeRoot
    foreach ($name in @('repository', 'scheduler', 'status')) { New-PrivateDirectory -Path (Join-Path $runtimeRoot $name) }
    $passwordFile = Join-Path $runtimeRoot 'postgres.pgpass'
    [IO.File]::WriteAllText($passwordFile, "127.0.0.1:$postgresPort`:*:postgres:$password`n", [Text.UTF8Encoding]::new($false))
    Invoke-Checked 'icacls.exe' @($passwordFile, '/inheritance:r', '/grant:r', "$((& whoami.exe).Trim())`:F", '*S-1-5-18:F') | Out-Null

    $postgresqlBin = Join-Path $packagePath 'postgresql/bin'
    $archivePath = Join-Path $runtimeRoot 'probe.dump'
    $processEnvironment = @{ PGPASSFILE = $passwordFile }
    foreach ($environmentEntry in $processEnvironment.GetEnumerator()) { [Environment]::SetEnvironmentVariable($environmentEntry.Key, $environmentEntry.Value) }
    try {
        Invoke-Checked (Join-Path $postgresqlBin 'pg_dump.exe') @('--host', '127.0.0.1', '--port', "$postgresPort",
            '--username', 'postgres', '--dbname', 'postgres', '--format', 'custom', '--file', $archivePath, '--no-password') | Out-Null
        $archiveList = Invoke-Checked (Join-Path $postgresqlBin 'pg_restore.exe') @('--list', $archivePath)
        if (($archiveList -join "`n") -notmatch 'TABLE DATA public package_probe') { throw 'Windows pg_restore did not read the real archive.' }
        Invoke-Checked (Join-Path $postgresqlBin 'createdb.exe') @('--host', '127.0.0.1', '--port', "$postgresPort",
            '--username', 'postgres', '--no-password', 'package_restore') | Out-Null
        Invoke-Checked (Join-Path $postgresqlBin 'pg_restore.exe') @('--host', '127.0.0.1', '--port', "$postgresPort",
            '--username', 'postgres', '--dbname', 'package_restore', '--no-password', '--exit-on-error', $archivePath) | Out-Null
        $restoredCount = Invoke-Checked (Join-Path $postgresqlBin 'psql.exe') @('-X', '-A', '-t', '-h', '127.0.0.1', '-p', "$postgresPort",
            '-U', 'postgres', '-d', 'package_restore', '--no-password', '-c', 'SELECT count(*) FROM package_probe')
        if (($restoredCount -join '').Trim() -ne '1') { throw 'Windows PostgreSQL client restore result is invalid.' }
    } finally {
        [Environment]::SetEnvironmentVariable('PGPASSFILE', $null)
    }
    Add-Step 'TOOLS: packaged Windows pg_dump/pg_restore/createdb/psql completed a real PostgreSQL 18.6 round trip'

    $futureAnchor = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 86400
    $configuration = [ordered]@{
        schema_version = 2
        deployment_id = $deploymentId
        repository_root = (Join-Path $runtimeRoot 'repository')
        offsite_repository_root = $null
        scheduler_root = (Join-Path $runtimeRoot 'scheduler')
        status_root = (Join-Path $runtimeRoot 'status')
        pg_dump_path = (Join-Path $runtimeRoot 'placeholder-pg_dump.exe')
        pg_dump_sha256 = ('1' * 64)
        pg_restore_path = (Join-Path $runtimeRoot 'placeholder-pg_restore.exe')
        pg_restore_sha256 = ('2' * 64)
        command_timeout_seconds = 60
        poll_interval_seconds = 1
        schedule = [ordered]@{ deployment_id = $deploymentId; anchor_unix = $futureAnchor; period_seconds = 3600 }
        retention = [ordered]@{ hourly = 24; daily = 7; weekly = 4; monthly = 6; pre_upgrade = 5; manual_days = 30 }
        offsite_retention = $null
        plan = [ordered]@{
            deployment_id = $deploymentId
            kind = 'independent'
            write_barrier_proof_file = $null
            retention = @('hourly')
            previous_recovery_set_id = $null
            targets = @(
                [ordered]@{ state = 'required'; database = [ordered]@{ service = 'console'; host = '127.0.0.1'; port = $postgresPort;
                    database = 'postgres'; username = 'postgres'; password_file = $passwordFile; schema_version = 1 } },
                [ordered]@{ state = 'not_applicable'; service = 'auth'; reason = 'not installed in package validation' },
                [ordered]@{ state = 'not_applicable'; service = 'desk'; reason = 'not installed in package validation' }
            )
        }
    }
    $configPath = Join-Path $runtimeRoot 'input-config.json'
    [IO.File]::WriteAllText($configPath, ($configuration | ConvertTo-Json -Depth 20 -Compress), [Text.UTF8Encoding]::new($false))

    $unexpectedLibrary = Join-Path $packagePath 'postgresql/bin/unexpected.dll'
    Copy-Item -LiteralPath (Join-Path $postgresqlBin 'libpq.dll') -Destination $unexpectedLibrary
    Invoke-Checked 'pwsh.exe' @('-NoProfile', '-File', (Join-Path $packagePath 'install.ps1'), '-PackageRoot', $packagePath,
        '-ExpectedPackageManifestSha256', $manifestHash, '-ConfigPath', $configPath,
        '-InstallationRoot', $installationRoot, '-DataRoot', $dataRoot) -ExpectFailure | Out-Null
    Remove-Item -LiteralPath $unexpectedLibrary -Force
    if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) { throw 'Rejected package created a service.' }
    Add-Step 'PACKAGE-ADMISSION: extra DLL and unreviewed package contents fail before installation'

    $installArguments = @('-NoProfile', '-File', (Join-Path $packagePath 'install.ps1'), '-PackageRoot', $packagePath,
        '-ExpectedPackageManifestSha256', $manifestHash, '-ConfigPath', $configPath,
        '-InstallationRoot', $installationRoot, '-DataRoot', $dataRoot)
    Invoke-Checked 'pwsh.exe' $installArguments | Out-Null
    if ((Get-Service -Name $serviceName).Status -ne 'Running') { throw 'Installed backup service is not running.' }
    $installedConfigPath = Join-Path $dataRoot "$deploymentId/backup/config.json"
    $installedConfig = Get-Content -LiteralPath $installedConfigPath -Raw | ConvertFrom-Json
    if ([string]$installedConfig.pg_dump_path -notlike "$installationRoot*" -or
        (Get-FileHash -LiteralPath ([string]$installedConfig.pg_dump_path) -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$installedConfig.pg_dump_sha256) {
        throw 'Installed configuration is not pinned to the installed PostgreSQL client.'
    }
    Invoke-Checked 'pwsh.exe' $installArguments | Out-Null
    if ((Get-Service -Name $serviceName).Status -ne 'Running') { throw 'Covering installation did not preserve service availability.' }
    Add-Step 'SCM: first install and covering install used versioned binaries, private config, virtual account and stable startup'

    $badConfig = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    $badConfig.repository_root = Join-Path $runtimeRoot 'missing-repository'
    $badConfigPath = Join-Path $runtimeRoot 'bad-config.json'
    [IO.File]::WriteAllText($badConfigPath, ($badConfig | ConvertTo-Json -Depth 20 -Compress), [Text.UTF8Encoding]::new($false))
    Invoke-Checked 'pwsh.exe' @('-NoProfile', '-File', (Join-Path $packagePath 'install.ps1'), '-PackageRoot', $packagePath,
        '-ExpectedPackageManifestSha256', $manifestHash, '-ConfigPath', $badConfigPath,
        '-InstallationRoot', $installationRoot, '-DataRoot', $dataRoot) -ExpectFailure | Out-Null
    if ((Get-Service -Name $serviceName).Status -ne 'Running' -or
        (Get-FileHash -LiteralPath $installedConfigPath -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath (Join-Path $dataRoot "$deploymentId/backup/config.previous.json") -Algorithm SHA256).Hash) {
        throw 'Failed covering install did not roll back the running service and configuration.'
    }
    Add-Step 'ROLLBACK: rejected covering configuration restored the previous running service state'

    Invoke-Checked 'pwsh.exe' @('-NoProfile', '-File', (Join-Path $packagePath 'uninstall.ps1'), '-DeploymentId', "$deploymentId") | Out-Null
    if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) { throw 'Backup service remained registered after uninstall.' }
    if (-not (Test-Path -LiteralPath $installedConfigPath) -or -not (Test-Path -LiteralPath (Join-Path $runtimeRoot 'repository'))) {
        throw 'Uninstall removed retained configuration or backup data.'
    }
    Add-Step 'UNINSTALL: service removed while configuration, schedules and recovery data remained intact'

    $report = [ordered]@{
        schema_version = 1
        run_id = $runId
        postgresql_archive_sha256 = (Get-FileHash -LiteralPath $resolvedArchive -Algorithm SHA256).Hash.ToLowerInvariant()
        package_manifest_sha256 = $manifestHash
        deployment_id = $deploymentId
        cases = @($steps)
    }
    [IO.File]::WriteAllText((Join-Path $reportRoot 'report.json'), ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    Write-Output "REPORT $reportRoot"
} finally {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -ne $service) {
        if ($service.Status -ne 'Stopped') { & sc.exe stop $serviceName 2>&1 | Out-Null }
        & sc.exe delete $serviceName 2>&1 | Out-Null
    }
    if ($containerStarted) { & docker.exe rm --force --volumes $containerName 2>&1 | Out-Null }
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
        $expectedParent = [IO.Path]::GetFullPath((Join-Path $repositoryRoot '.cache'))
        if ([IO.Path]::GetDirectoryName($resolvedTestRoot) -cne $expectedParent -or [IO.Path]::GetFileName($resolvedTestRoot) -cne $runId) {
            throw 'Refusing unsafe Windows package test cleanup.'
        }
        & takeown.exe /F $resolvedTestRoot /R /D Y 2>&1 | Out-Null
        & icacls.exe $resolvedTestRoot /grant "$((& whoami.exe).Trim())`:(OI)(CI)F" /T /C 2>&1 | Out-Null
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
