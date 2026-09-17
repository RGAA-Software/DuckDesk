#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [ValidateSet('Up', 'Test', 'TestSuite', 'PrepareQueries', 'Down', 'Status')]
    [string]$Action = 'Status',
    [ValidateRange(0,65535)]
    [int]$Port = 0,
    [switch]$Linux,
    [ValidateSet('', 'unit', 'identity', 'control', 'devices', 'applications', 'guests', 'nodes', 'deployments', 'instances', 'commands', 'workspaces', 'database', 'sessions', 'transfers', 'recordings', 'preferences', 'files', 'backup', 'backup-pg', 'cache', 'activity', 'updates', 'desk', 'auth', 'auth-api', 'catalog', 'lease', 'postgres', 'schema_gate', 'accounts', 'console-api', 'directory-api', 'node-control', 'console-process', 'console-admin')]
    [string]$Suite = ''
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if ($Action -eq 'TestSuite') {
    if (-not $Suite) { throw 'TestSuite requires an explicit -Suite' }
    if ($Linux) { throw 'TestSuite is a focused native check. Use Test -Linux for the full cross-platform gate.' }
} elseif ($Suite) { throw '-Suite is only valid with TestSuite' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dockerBin = 'C:\Program Files\Docker\Docker\resources\bin'
if (Test-Path -LiteralPath $dockerBin) { $env:Path = "$dockerBin;$env:Path" }
$runId = 'pg-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
$project = if ($Action -in @('Test','TestSuite','PrepareQueries')) { "pixels-$runId" } else { 'pixels-pg-dev' }
$localDir = Join-Path $repo ".env/$project"
$envFile = Join-Path $localDir 'postgres.env'
$reportDir = Join-Path $repo "test-results/server_validation/$runId"
$targetDir = Join-Path $repo '.cache/pg-cargo'
$composeFile = Join-Path $repo 'deploy/development/postgres/compose.yaml'
$manifest = Join-Path $repo 'rust_server/Cargo.toml'
$composeArgs = @('compose','--project-name',$project,'--env-file',$envFile,'--file',$composeFile)
$secrets = @{}
$savedEnv = @{}
$steps = [Collections.Generic.List[object]]::new()
$started = $false
$failed = $false
$commandIndex = 0
$revision = (& git -C $repo rev-parse HEAD).Trim()
$fingerprints = @{}
$sourceFiles = @('rust_server/Cargo.toml','rust_server/Cargo.lock')
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'rust_server/px_credentials'),(Join-Path $repo 'rust_server/px_console_server/runtime') -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'rust_server/px_node_protocol') -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'rust_server/px_backup') -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
$sourceFiles += @('rust_server/px_auth_server/Cargo.toml','rust_server/px_auth_server/build.rs')
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'rust_server/px_auth_server/src'),(Join-Path $repo 'rust_server/px_auth_server/tests') -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
$sourceFiles += @('web/px_pixels/package.json','web/px_pixels/package-lock.json','web/px_pixels/vite.config.ts','web/px_pixels/index.html')
$sourceFiles += @('web/px_auth/package.json','web/px_auth/package-lock.json','web/px_auth/vite.config.ts','web/px_auth/vitest.config.ts','web/px_auth/index.html')
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'web/px_auth/src') -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'web/px_pixels/src') -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'rust_server/px_auth_server/license') -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'rust_server/px_auth_server/storage') -File -Recurse -Force | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'rust_server/px_pg'),(Join-Path $repo 'rust_server/px_private_files'),(Join-Path $repo 'rust_server/px_release_catalog'),(Join-Path $repo 'deploy/development/postgres'),$PSScriptRoot -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
$sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo 'rust_server/px_desk_server'),(Join-Path $repo 'rust_server/px_console_server/storage') -File -Recurse -Force | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
foreach ($service in @('console','auth','desk')) {
    $sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $repo "rust_server/px_${service}_server/migrations") -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) })
}
if ($Action -eq 'TestSuite') {
    # Focused native checks do not build either web application. Excluding them also lets
    # frontend work continue without invalidating an unrelated long-running native test.
    $sourceFiles = @($sourceFiles | Where-Object { $_ -notmatch '^web[\\/]' })
}
$sourceHashes = @{}
foreach ($file in ($sourceFiles | Sort-Object -Unique)) {
    $sourceHashes[$file] = (Get-FileHash -LiteralPath (Join-Path $repo $file) -Algorithm SHA256).Hash
}

function Set-LocalEnv([string]$Name, [string]$Value) {
    if (-not $savedEnv.ContainsKey($Name)) { $savedEnv[$Name] = [Environment]::GetEnvironmentVariable($Name) }
    [Environment]::SetEnvironmentVariable($Name, $Value)
}

function Invoke-Checked([string]$Exe, [string[]]$Arguments, [switch]$ExpectFailure, [int]$TimeoutSeconds = 600) {
    $script:commandIndex++
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Exe
    $start.WorkingDirectory = $repo
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $timedOut = $false
    try {
        if (-not $process.Start()) { throw 'Could not start validation command' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $timedOut = $true
            $process.Kill($true)
            if (-not $process.WaitForExit(10000)) { throw 'Timed-out validation process could not be reaped' }
        }
        $code = $process.ExitCode
        $output = $stdout.GetAwaiter().GetResult() + "`n" + $stderr.GetAwaiter().GetResult()
    } finally { $process.Dispose() }
    foreach ($entry in $secrets.GetEnumerator()) {
        if ($entry.Key -like '*PASSWORD') { $output = $output.Replace($entry.Value, '<redacted>') }
    }
    if (Test-Path -LiteralPath $reportDir) {
        [IO.File]::WriteAllText((Join-Path $reportDir ("command-{0:D3}.log" -f $script:commandIndex)), $output)
    }
    if ($timedOut) { throw "Validation command exceeded its deadline: $([IO.Path]::GetFileName($Exe)); limit=${TimeoutSeconds}s" }
    $success = if ($ExpectFailure) { $code -ne 0 } else { $code -eq 0 }
    if (-not $success) {
        Write-Host $output
        throw "Command failed its expected exit condition: $([IO.Path]::GetFileName($Exe)); exit=$code"
    }
    return $output
}

function Invoke-Compose([string[]]$Arguments) { Invoke-Checked 'docker' ($composeArgs + $Arguments) }

function Add-Step([string]$Name) {
    $steps.Add([ordered]@{case=$Name; status='PASS'})
    Write-Host "PASS $Name"
}

function Add-TestCases([string]$Output, [string]$Prefix, [int]$Expected) {
    $results = [regex]::Matches($Output, '(?m)^test ([^\r\n]+?) \.\.\. (ok|FAILED|ignored)\s*$')
    if ($results.Count -ne $Expected) { throw "Unexpected test count: $Prefix; expected=$Expected actual=$($results.Count)" }
    foreach ($result in $results) {
        if ($result.Groups[2].Value -ne 'ok') { throw "Required test did not pass: $Prefix/$($result.Groups[1].Value)" }
        $steps.Add([ordered]@{case="$Prefix/$($result.Groups[1].Value)"; status='PASS'})
    }
}

function Assert-SourceHashes {
    foreach ($entry in $sourceHashes.GetEnumerator()) {
        $path = Join-Path $repo $entry.Key
        if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Value) {
            throw "Source changed during acceptance: $($entry.Key)"
        }
    }
    Add-Step 'SOURCE: all recorded source hashes unchanged throughout acceptance'
}

function Use-Service([string]$Service, [string]$Role) {
    $key = "$($Service.ToUpper())_$($Role.ToUpper())_PASSWORD"
    $dsn = "postgresql://pixels_${Service}_${Role}:$($secrets[$key])@127.0.0.1:$($secrets.PG_PORT)/pixels_$Service"
    Set-LocalEnv 'PIXELS_DATABASE_URL' $dsn
}

try {
    if ($Action -in @('Down','Status') -and -not (Test-Path -LiteralPath $envFile)) {
        throw 'No development environment exists. Run postgres.ps1 Up first.'
    }
    New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
    Invoke-Checked 'pwsh' @('-NoProfile','-File',(Join-Path $repo 'scripts/check_readable_names.ps1')) | Out-Null
    Add-Step 'SOURCE-NAMING: maintained DB0-DB5 and Windows Service Rust identifiers are human-readable'
    if (-not (Test-Path -LiteralPath $envFile)) {
        if ($Port -gt 0 -and $Port -lt 1024) { throw 'Explicit database port must be at least 1024' }
        $requestedPort = if ($Action -in @('Test','TestSuite','PrepareQueries')) { 0 } else { $Port }
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,$requestedPort)
        try {
            $listener.Start()
            $Port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
        } finally { $listener.Stop() }
        $secrets.PG_PORT = "$Port"
        $secrets.PIXELS_DEPLOYMENT_ID = [guid]::NewGuid().ToString()
        foreach ($key in @('PG_ADMIN_PASSWORD','CONSOLE_OWNER_PASSWORD','CONSOLE_RUNTIME_PASSWORD','AUTH_OWNER_PASSWORD','AUTH_RUNTIME_PASSWORD','DESK_OWNER_PASSWORD','DESK_RUNTIME_PASSWORD')) {
            $secrets[$key] = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)).ToLowerInvariant()
        }
        New-Item -ItemType Directory -Path $localDir -Force | Out-Null
        # Restrict generated local credentials before writing them.
        if ($IsWindows) {
            $identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
            Invoke-Checked 'icacls' @($localDir,'/inheritance:r','/grant:r',"${identity}:(OI)(CI)F",'SYSTEM:(OI)(CI)F') | Out-Null
        } else {
            Invoke-Checked 'chmod' @('700',$localDir) | Out-Null
        }
        $envText = ($secrets.GetEnumerator() | Sort-Object Key | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join "`n"
        [IO.File]::WriteAllText($envFile, "$envText`n")
    } else {
        foreach ($line in Get-Content -LiteralPath $envFile) {
            if ($line -match '^([A-Z_]+)=(.+)$') { $secrets[$Matches[1]] = $Matches[2] }
        }
    }
    if ($Action -eq 'Status') { Write-Host (Invoke-Compose @('ps')); return }
    if ($Action -eq 'Down') {
        Write-Host (Invoke-Compose @('down','--timeout','15'))
        Write-Host 'Development volume and local credentials retained.'
        return
    }
    Invoke-Checked 'docker' @('info','--format','{{.ServerVersion}}') | Out-Null
    $fingerprints.rustc = (Invoke-Checked 'rustc' @('--version')).Trim()
    $fingerprints.compose = (Invoke-Checked 'docker' @('compose','version','--short')).Trim()
    Invoke-Compose @('config','--quiet') | Out-Null
    $started = $true
    Invoke-Compose @('up','--detach','--wait','--wait-timeout','120') | Out-Null
    $container = (Invoke-Compose @('ps','--quiet','postgres')).Trim()
    if ($container -notmatch '^[a-f0-9]{12,64}$') { throw 'Invalid container identity' }
    $label = (Invoke-Checked 'docker' @('inspect',$container,'--format','{{index .Config.Labels "com.docker.compose.project"}}')).Trim()
    if ($label -ne $project) { throw 'Container project identity mismatch' }
    Add-Step 'ENV: isolated PostgreSQL healthy'
    $fingerprints.image = (Invoke-Checked 'docker' @('inspect',$container,'--format','{{.Image}}')).Trim()
    Invoke-Checked 'cargo' @('build','--locked','--manifest-path',$manifest,'-p','px_pg','--target-dir',$targetDir) | Out-Null
    $exeName = if ($IsWindows) { 'px_db.exe' } else { 'px_db' }
    $dbTool = Join-Path $targetDir "debug/$exeName"
    $fingerprints.px_db = (Get-FileHash -LiteralPath $dbTool -Algorithm SHA256).Hash
    $fingerprints.px_db_initial_schema = $fingerprints.px_db
    $fingerprints.cargo_lock = (Get-FileHash -LiteralPath (Join-Path $repo 'rust_server/Cargo.lock') -Algorithm SHA256).Hash
    Set-LocalEnv 'PIXELS_DEPLOYMENT_ID' $secrets.PIXELS_DEPLOYMENT_ID
    Set-LocalEnv 'PIXELS_PG_LOCAL_DEVELOPMENT' '1'
    Set-LocalEnv 'PIXELS_TEST_PG_ADMIN_PASSWORD' $secrets.PG_ADMIN_PASSWORD
    foreach ($service in @('console','auth','desk')) {
        Use-Service $service 'owner'
        if ($Action -eq 'Test') {
            $pre = Invoke-Checked $dbTool @('check',$service) -ExpectFailure
            if ($pre -notmatch 'schema missing') { throw 'Fresh schema check failed for an unexpected reason' }
        }
        Invoke-Checked $dbTool @('migrate',$service) | Out-Null
        Invoke-Checked $dbTool @('migrate',$service) | Out-Null
        Use-Service $service 'runtime'
        Invoke-Checked $dbTool @('check',$service) | Out-Null
        foreach ($role in @('owner','runtime')) {
            Use-Service $service $role
            Set-LocalEnv "PIXELS_TEST_$($service.ToUpper())_$($role.ToUpper())_URL" $env:PIXELS_DATABASE_URL
        }
    }
    Add-Step 'SCHEMA: three fresh databases, repeatable initialization, runtime readiness'
    if ($Action -eq 'Up') {
        Write-Host "Development PostgreSQL ready on 127.0.0.1:$($secrets.PG_PORT); local configuration: $envFile"
        return
    }
    if ($Action -eq 'PrepareQueries') {
        # Explicit developer command, never performed implicitly by acceptance tests.
        # PostgreSQL/SQLx generate these files; this is not evidence that runtime tests passed.
        foreach ($item in @(
            @{Service='console';Crate='px_console_store';Path='rust_server/px_console_server/storage';Count=237},
            @{Service='desk';Crate='px_desk_server';Path='rust_server/px_desk_server';Count=9},
            @{Service='auth';Crate='px_auth_store';Path='rust_server/px_auth_server/storage';Count=30}
        )) {
            Use-Service $item.Service 'runtime'
            Set-LocalEnv 'DATABASE_URL' $env:PIXELS_DATABASE_URL
            Set-LocalEnv 'SQLX_OFFLINE' 'false'
            $generated = (New-Item -ItemType Directory -Path (Join-Path $reportDir "$($item.Service)-sqlx") -Force).FullName
            Set-LocalEnv 'SQLX_OFFLINE_DIR' $generated
            Invoke-Checked 'cargo' @('check','--locked','--manifest-path',$manifest,'-p',$item.Crate,'--target-dir',$targetDir) | Out-Null
            $queries = @(Get-ChildItem -LiteralPath $generated -Filter 'query-*.json' -File)
            if ($queries.Count -ne $item.Count) { throw "Unexpected generated query count: $($item.Service)" }
            $destination = (New-Item -ItemType Directory -Path (Join-Path $repo "$($item.Path)/.sqlx") -Force).FullName
            foreach ($query in $queries) { Copy-Item -LiteralPath $query.FullName -Destination (Join-Path $destination $query.Name) }
            # Only SQLx-generated hashes in this crate's explicit cache can become obsolete.
            foreach ($cached in Get-ChildItem -LiteralPath $destination -Filter 'query-*.json' -File) {
                if ($cached.Name -notmatch '^query-[a-f0-9]{64}\.json$') { throw 'Unexpected SQLx cache file name' }
                if ($cached.Name -notin $queries.Name) { Remove-Item -LiteralPath $cached.FullName }
            }
            Add-Step "PREPARE-ONLY: $($item.Service) generated SQLx metadata; acceptance not run"
        }
        return
    }
    Set-LocalEnv 'PIXELS_PG_ISOLATED_TEST' '1'
    Set-LocalEnv 'PIXELS_TEST_CONTAINER' $container
    # Dedicated empty fixture databases keep bootstrap/last-administrator assertions platform-independent.
    foreach ($service in @('auth','console')) {
        $fixtureKinds = if ($service -eq 'auth') { @('bootstrap') } else { @('control','bootstrap','api','directory','node_control','process','admin') }
        foreach ($fixtureKind in $fixtureKinds) {
        foreach ($platform in @('windows','linux')) {
            $fixtureDb = "pixels_${service}_${fixtureKind}_$platform"
            Invoke-Checked 'docker' @('exec',$container,'createdb','-U','pixels_admin','-O',"pixels_${service}_owner",'-T',"pixels_$service",$fixtureDb) | Out-Null
            Invoke-Checked 'docker' @('exec',$container,'psql','-X','-v','ON_ERROR_STOP=1','-U','pixels_admin','-d',$fixtureDb,'-c',
                "REVOKE ALL ON DATABASE $fixtureDb FROM PUBLIC; GRANT CONNECT ON DATABASE $fixtureDb TO pixels_${service}_owner,pixels_${service}_runtime; REVOKE CREATE ON SCHEMA public FROM PUBLIC") | Out-Null
            Use-Service $service 'owner'
            Set-LocalEnv 'PIXELS_DATABASE_URL' ($env:PIXELS_DATABASE_URL -replace "/pixels_$service$","/$fixtureDb")
            Invoke-Checked $dbTool @('migrate',$service) | Out-Null
        }
        }
    }
    if ($Action -eq 'Test') {
        # Infrastructure tests use their own synthetic table, not a product domain schema.
        # Create it before taking the Linux baseline so both platform runs start identically.
        Invoke-Checked 'docker' @('exec',$container,'psql','-X','-v','ON_ERROR_STOP=1','-U','pixels_admin','-d','pixels_desk','-c',
            "CREATE TABLE pixels.pg_fixture(id uuid PRIMARY KEY,version text NOT NULL,created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP); ALTER TABLE pixels.pg_fixture OWNER TO pixels_desk_owner; GRANT SELECT,INSERT,UPDATE,DELETE ON pixels.pg_fixture TO pixels_desk_runtime") | Out-Null
    }
    if ($Action -eq 'Test' -and $Linux) {
        foreach ($service in @('console','auth','desk')) {
            $baselineDb = "pixels_${service}_linux_baseline"
            Invoke-Checked 'docker' @('exec',$container,'createdb','-U','pixels_admin','-O',"pixels_${service}_owner",'-T',"pixels_$service",$baselineDb) | Out-Null
            Invoke-Checked 'docker' @('exec',$container,'psql','-X','-v','ON_ERROR_STOP=1','-U','pixels_admin','-d',$baselineDb,'-c',
                "REVOKE ALL ON DATABASE $baselineDb FROM PUBLIC; GRANT CONNECT ON DATABASE $baselineDb TO pixels_${service}_owner,pixels_${service}_runtime; REVOKE CREATE ON SCHEMA public FROM PUBLIC") | Out-Null
        }
        Add-Step 'LINUX-BASELINE: pristine three-database snapshot isolated before Windows tests'
    }
    if ($Action -eq 'TestSuite') {
        if ($Suite -eq 'postgres') {
            Invoke-Checked 'docker' @('exec',$container,'psql','-X','-v','ON_ERROR_STOP=1','-U','pixels_admin','-d','pixels_desk','-c',
                "CREATE TABLE pixels.pg_fixture(id uuid PRIMARY KEY,version text NOT NULL,created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP); ALTER TABLE pixels.pg_fixture OWNER TO pixels_desk_owner; GRANT SELECT,INSERT,UPDATE,DELETE ON pixels.pg_fixture TO pixels_desk_runtime") | Out-Null
        }
        $suiteCounts = @{unit=19;identity=12;control=8;devices=8;applications=8;guests=9;nodes=7;deployments=6;instances=10;commands=16;workspaces=6;database=2;sessions=10;transfers=8;recordings=6;preferences=7;files=8;backup=61;'backup-pg'=1;cache=16;activity=8;updates=7;desk=7;catalog=4;lease=6;postgres=14;accounts=9}
        $suiteCounts['console-api'] = 5
        $suiteCounts['directory-api'] = 5
        $suiteCounts['node-control'] = 1
        $suiteCounts['console-process'] = 1
        $suiteCounts['console-admin'] = 2
        $suiteCounts['schema_gate'] = 4
        $suiteCounts['auth'] = 7
        $suiteCounts['auth-api'] = 9
        Set-LocalEnv 'SQLX_OFFLINE' 'true'
        Set-LocalEnv 'SQLX_OFFLINE_DIR' (Join-Path $repo 'rust_server/px_console_server/storage/.sqlx')
        if ($Suite -in @('console-api','directory-api','node-control','console-process','console-admin')) {
            $apiTest = if($Suite -eq 'console-api'){'identity_api'}elseif($Suite -eq 'directory-api'){'directory_api'}elseif($Suite -eq 'node-control'){'node_control'}elseif($Suite -eq 'console-process'){'process'}else{'admin'}
            $suiteArgs = @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_console_runtime','--features','pg-integration','--test',$apiTest,'--target-dir',$targetDir)
        } elseif ($Suite -eq 'files') {
            $suiteArgs = @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_private_files','--features','integration-probe','--test','cache_files','--target-dir',$targetDir)
        } elseif ($Suite -eq 'backup') {
            $suiteArgs = @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_backup','--all-targets','--target-dir',$targetDir)
        } elseif ($Suite -eq 'backup-pg') {
            $suiteArgs = @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_backup','--features','pg-integration','--test','postgres','--target-dir',$targetDir)
        } elseif ($Suite -eq 'desk') {
            $suiteArgs = @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_desk_server','--features','pg-integration','--test','postgres_api','--target-dir',$targetDir)
        } elseif ($Suite -in @('auth','auth-api')) {
            $authCrate = if($Suite -eq 'auth'){'px_auth_store'}else{'px_auth_server'}
            $authTest = if($Suite -eq 'auth'){'issuance'}else{'postgres_api'}
            $suiteArgs = @('test','--offline','--locked','--manifest-path',$manifest,'-p',$authCrate,'--features','pg-integration','--test',$authTest,'--target-dir',$targetDir)
        } elseif ($Suite -in @('lease','postgres','schema_gate')) {
            $suiteArgs = @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_pg','--features','pg-integration','--test',$Suite,'--target-dir',$targetDir)
        } elseif ($Suite -eq 'catalog') {
            $suiteArgs = @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_release_catalog','--lib','--target-dir',$targetDir)
        } else {
            $suiteArgs = @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--target-dir',$targetDir)
            if ($Suite -eq 'unit') { $suiteArgs += '--lib' } else { $suiteArgs += @('--test',$Suite) }
        }
        $suiteArgs += @('--','--test-threads=1')
        $result = Invoke-Checked 'cargo' $suiteArgs
        Write-Host $result
        Add-TestCases $result "focused/$Suite" $suiteCounts[$Suite]
        $fingerprints.px_db = (Get-FileHash -LiteralPath $dbTool -Algorithm SHA256).Hash
        Assert-SourceHashes
        Add-Step 'FOCUSED-ONLY: selected native suite; no browser, restore or full cross-platform acceptance'
        return
    }
    $catalogTests = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_release_catalog','--lib','--target-dir',$targetDir)
    Write-Host $catalogTests
    Add-TestCases $catalogTests 'native/release-catalog' 4
    $fileTests = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_private_files','--features','integration-probe','--test','cache_files','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $fileTests
    Add-TestCases $fileTests 'native/private-files' 8
    Add-Step 'FILES: private anchored roots, process locks, immutable hash-verified blobs and exact cleanup'
    $backupTests = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_backup','--all-targets','--target-dir',$targetDir)
    Write-Host $backupTests
    Add-TestCases $backupTests 'native/backup-core' 61
    Add-Step 'BACKUP-DAEMON: recovery sets, retention, persistent scheduling, private status/alerts, pinned tools and Windows SCM target compile'
    $backupIntegration = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_backup','--features','pg-integration','--test','postgres','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $backupIntegration
    Add-TestCases $backupIntegration 'native/backup-postgres' 1
    $licenseTests = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_license','--test','contract','--target-dir',$targetDir)
    Add-TestCases $licenseTests 'native/license-contract' 7
    $unit = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_pg','--lib','--target-dir',$targetDir)
    Add-TestCases $unit 'native/pg-unit' 2
    Add-Step 'CONFIG: redaction and transport rejection'
    $integration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_pg','--features','pg-integration','--test','postgres','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $integration
    Add-TestCases $integration 'native/pg-integration' 14
    $leaseTests = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_pg','--features','pg-integration','--test','lease','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $leaseTests
    Add-TestCases $leaseTests 'native/pg-lease' 6
    $schemaGateTests = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_pg','--features','pg-integration','--test','schema_gate','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $schemaGateTests
    Add-TestCases $schemaGateTests 'native/schema-gate' 4
    Add-Step 'SCHEMA-GATE: every pooled backend pins schema; real migrator/startup/reconnect/process death/cancellation races'
    Add-Step 'LEASE: dedicated process lock, takeover quiet interval, terminal expiry, backend loss and OS-process death'
    Add-Step 'PG: fourteen database integration cases, including recovery watermarks, runtime role gates, OS process kill and two-process migration retry'
    Use-Service 'console' 'runtime'
    Set-LocalEnv 'DATABASE_URL' $env:PIXELS_DATABASE_URL
    Set-LocalEnv 'SQLX_OFFLINE' 'false'
    $queryMetadata = (New-Item -ItemType Directory -Path (Join-Path $reportDir 'sqlx') -Force).FullName
    Set-LocalEnv 'SQLX_OFFLINE_DIR' $queryMetadata
    Invoke-Checked 'cargo' @('check','--locked','--manifest-path',$manifest,'-p','px_console_store','--target-dir',$targetDir) | Out-Null
    $committedMetadata = Join-Path $repo 'rust_server/px_console_server/storage/.sqlx'
    $expectedQueries = @(Get-ChildItem -LiteralPath $committedMetadata -Filter 'query-*.json' -File)
    $actualQueries = @(Get-ChildItem -LiteralPath $queryMetadata -Filter 'query-*.json' -File)
    if ($expectedQueries.Count -ne 237 -or $actualQueries.Count -ne $expectedQueries.Count) { throw 'Missing or extra SQLx query metadata' }
    foreach ($expected in $expectedQueries) {
        $actual = Join-Path $queryMetadata $expected.Name
        if (-not (Test-Path -LiteralPath $actual) -or (Get-FileHash -LiteralPath $expected.FullName).Hash -ne (Get-FileHash -LiteralPath $actual).Hash) {
            throw "SQLx metadata differs from clean PostgreSQL schema: $($expected.Name)"
        }
    }
    Set-LocalEnv 'SQLX_OFFLINE' 'true'
    Set-LocalEnv 'SQLX_OFFLINE_DIR' $committedMetadata
    Add-Step 'QUERY: 237 Console SQLx queries compiled against fresh PG; offline metadata matches'
    $identityUnit = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--lib','--target-dir',$targetDir)
    Add-TestCases $identityUnit 'native/identity-unit' 19
    $identityIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','identity','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $identityIntegration
    Add-TestCases $identityIntegration 'native/identity-integration' 12
    Add-Step 'IDENTITY: twelve repository cases including 100 login/revocation races and atomic membership updates'
    $accountIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','accounts','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $accountIntegration
    Add-TestCases $accountIntegration 'native/accounts' 9
    Add-Step 'ACCOUNTS: empty bootstrap, exact login binding, password/logout races and restricted identity privileges'
    $consoleUnit = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_console_runtime','--lib','--target-dir',$targetDir)
    Write-Host $consoleUnit
    Add-TestCases $consoleUnit 'native/console-ingress' 3
    $nodeProtocolUnit = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_node_protocol','--lib','--target-dir',$targetDir)
    Write-Host $nodeProtocolUnit
    Add-TestCases $nodeProtocolUnit 'native/node-protocol' 1
    $consoleApi = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_console_runtime','--features','pg-integration','--test','identity_api','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $consoleApi
    Add-TestCases $consoleApi 'native/console-identity-api' 5
    $directoryApi = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_console_runtime','--features','pg-integration','--test','directory_api','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $directoryApi
    Add-TestCases $directoryApi 'native/console-directory-api' 5
    $nodeControl = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_console_runtime','--features','pg-integration','--test','node_control','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $nodeControl
    Add-TestCases $nodeControl 'native/console-node-control' 1
    $consoleProcess = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_console_runtime','--features','pg-integration','--test','process','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $consoleProcess
    Add-TestCases $consoleProcess 'native/console-process' 1
    Add-Step 'CONSOLE-PROCESS: real listener readiness and terminal database-authority loss'
    $consoleAdmin = Invoke-Checked 'cargo' @('test','--offline','--locked','--manifest-path',$manifest,'-p','px_console_runtime','--features','pg-integration','--test','admin','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $consoleAdmin
    Add-TestCases $consoleAdmin 'native/console-admin' 2
    Add-Step 'CONSOLE-ADMIN: explicit private secret generation and owner-only empty-database bootstrap'
    $controlIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','control','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $controlIntegration
    Add-TestCases $controlIntegration 'native/control' 8
    Add-Step 'CONTROL: roles, last administrator, atomic revocation/audit, bounded gates and leased outbox'
    $deviceIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','devices','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $deviceIntegration
    Add-TestCases $deviceIntegration 'native/devices' 8
    Add-Step 'DEVICES: typed identities, ACL, transactional revocation/audit, CAS, rotation and soft deletion'
    $appIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','applications','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $appIntegration
    Add-TestCases $appIntegration 'native/applications' 8
    Add-Step 'APPLICATIONS: three modes, ACL/card isolation, CAS, atomic events, rollback and durable soft deletion'
    $guestIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','guests','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $guestIntegration
    Add-TestCases $guestIntegration 'native/guests' 9
    Add-Step 'GUESTS: distinct identity/client type, public ACL, expiry, atomic source/session blocks, management and leased events'
    $nodeIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','nodes','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $nodeIntegration
    Add-TestCases $nodeIntegration 'native/nodes' 7
    Add-Step 'NODES: authenticated generations, ordered reports, restart reconciliation, CAS, rotation and atomic rollback'
    $deploymentIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','deployments','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $deploymentIntegration
    Add-TestCases $deploymentIntegration 'native/deployments' 6
    Add-Step 'DEPLOYMENTS: mode contracts, unique identity, preparation revision/generation, roles, CAS and audit rollback'
    $instanceIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','instances','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $instanceIntegration
    Add-TestCases $instanceIntegration 'native/instances' 10
    Add-Step 'INSTANCES: two-process last-slot race, owner/client identity, exact request retry, gates and atomic command/event reservation'
    $commandIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','commands','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $commandIntegration
    Add-TestCases $commandIntegration 'native/commands' 16
    Add-Step 'COMMANDS: leased claims, revocation, exact acknowledgments, idempotent stop, uncertain occupancy and fenced inventory challenges'
    $workspaceIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','workspaces','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $workspaceIntegration
    Add-TestCases $workspaceIntegration 'native/workspaces' 6
    Add-Step 'WORKSPACES: persistent identity, protected command lease, AAD, SID pinning, rewrap CAS and atomic audit rollback'
    $databaseIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','database','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $databaseIntegration
    Add-TestCases $databaseIntegration 'native/composition' 2
    $sessionIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','sessions','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $sessionIntegration
    Add-TestCases $sessionIntegration 'native/sessions' 10
    $transferIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','transfers','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $transferIntegration
    Add-TestCases $transferIntegration 'native/transfers' 8
    $recordingIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','recordings','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $recordingIntegration
    Add-TestCases $recordingIntegration 'native/recordings' 6
    $preferenceIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','preferences','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $preferenceIntegration
    Add-TestCases $preferenceIntegration 'native/preferences' 7
    $cacheIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','cache','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $cacheIntegration
    Add-TestCases $cacheIntegration 'native/cache' 16
    $activityIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','activity','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $activityIntegration
    Add-TestCases $activityIntegration 'native/activity' 8
    $updateIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_console_store','--features','pg-integration','--test','updates','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $updateIntegration
    Add-TestCases $updateIntegration 'native/updates' 7
    Add-Step 'PREFERENCES: owner/client-scoped targets, bounded settings, exact retry, CAS, quota and atomic events'
    Add-Step 'RECORDINGS: immutable source versions, independent library, exact node/session origin, observation ordering and device ACL'
    Add-Step 'TRANSFERS: original producer, ordered idempotent progress, hash completion, unknown state and atomic events'
    Add-Step 'SESSIONS: explicit targets, original owner, descriptor leases, RDP occupancy, frontend retirement and atomic events'
    Add-Step 'COMPOSITION: one bounded shared pool, repository lifecycle, wrong deployment and owner-role rejection'
    Use-Service 'desk' 'runtime'
    Set-LocalEnv 'DATABASE_URL' $env:PIXELS_DATABASE_URL
    Set-LocalEnv 'SQLX_OFFLINE' 'false'
    $deskMetadata = (New-Item -ItemType Directory -Path (Join-Path $reportDir 'desk-sqlx') -Force).FullName
    Set-LocalEnv 'SQLX_OFFLINE_DIR' $deskMetadata
    Invoke-Checked 'cargo' @('check','--locked','--manifest-path',$manifest,'-p','px_desk_server','--target-dir',$targetDir) | Out-Null
    $deskCommitted = Join-Path $repo 'rust_server/px_desk_server/.sqlx'
    $deskExpected = @(Get-ChildItem -LiteralPath $deskCommitted -Filter 'query-*.json' -File)
    $deskActual = @(Get-ChildItem -LiteralPath $deskMetadata -Filter 'query-*.json' -File)
    if ($deskExpected.Count -ne 9 -or $deskActual.Count -ne 9) { throw 'Desk SQLx metadata must contain exactly nine queries' }
    foreach ($expected in $deskExpected) {
        $actual = Join-Path $deskMetadata $expected.Name
        if (-not (Test-Path -LiteralPath $actual) -or (Get-FileHash -LiteralPath $expected.FullName).Hash -ne (Get-FileHash -LiteralPath $actual).Hash) {
            throw "Desk SQLx metadata differs: $($expected.Name)"
        }
    }
    Set-LocalEnv 'SQLX_OFFLINE' 'true'
    # Each crate uses its own committed .sqlx directory; do not force Console's cache on Desk/Linux.
    Set-LocalEnv 'SQLX_OFFLINE_DIR' ''
    Add-Step 'QUERY: nine Desk queries compiled online; offline metadata matches'
    $deskIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_desk_server','--features','pg-integration','--test','postgres_api','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $deskIntegration
    Add-TestCases $deskIntegration 'native/desk-api' 7
    Add-Step 'DESK: real router/PG authorization, idempotency, CAS, release dimensions and rejected writes'
    Use-Service 'auth' 'runtime'
    Set-LocalEnv 'DATABASE_URL' $env:PIXELS_DATABASE_URL
    Set-LocalEnv 'SQLX_OFFLINE' 'false'
    $authMetadata = (New-Item -ItemType Directory -Path (Join-Path $reportDir 'auth-sqlx') -Force).FullName
    Set-LocalEnv 'SQLX_OFFLINE_DIR' $authMetadata
    Invoke-Checked 'cargo' @('check','--locked','--manifest-path',$manifest,'-p','px_auth_store','--target-dir',$targetDir) | Out-Null
    $authCommitted = Join-Path $repo 'rust_server/px_auth_server/storage/.sqlx'
    $authExpected = @(Get-ChildItem -LiteralPath $authCommitted -Filter 'query-*.json' -File)
    $authActual = @(Get-ChildItem -LiteralPath $authMetadata -Filter 'query-*.json' -File)
    if ($authExpected.Count -ne 30 -or $authActual.Count -ne 30) { throw 'Auth SQLx metadata must contain exactly 30 queries' }
    foreach ($expected in $authExpected) {
        $actual = Join-Path $authMetadata $expected.Name
        if (-not (Test-Path -LiteralPath $actual) -or (Get-FileHash -LiteralPath $expected.FullName).Hash -ne (Get-FileHash -LiteralPath $actual).Hash) {
            throw "Auth SQLx metadata differs: $($expected.Name)"
        }
    }
    Set-LocalEnv 'SQLX_OFFLINE' 'true'
    Set-LocalEnv 'SQLX_OFFLINE_DIR' ''
    Add-Step 'QUERY: 30 Auth queries compiled online; offline metadata matches'
    $authIntegration = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_auth_store','--features','pg-integration','--test','issuance','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $authIntegration
    Add-TestCases $authIntegration 'native/auth-issuance' 7
    Add-Step 'AUTH: transactional issuance, exact retry, concurrent CAS, revocation, denied writes and injected rollback'
    $authUnit = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_credentials','--lib','--target-dir',$targetDir)
    Add-TestCases $authUnit 'native/auth-security' 2
    $authApi = Invoke-Checked 'cargo' @('test','--locked','--manifest-path',$manifest,'-p','px_auth_server','--features','pg-integration','--test','postgres_api','--target-dir',$targetDir,'--','--test-threads=1')
    Write-Host $authApi
    Add-TestCases $authApi 'native/auth-api' 9
    Add-Step 'AUTH-API: native startup, private file ACL, bootstrap races, login, roles, signing and revocation'
    $fingerprints.px_auth = (Get-FileHash -LiteralPath (Join-Path $targetDir 'debug/px_auth.exe')).Hash
    $fingerprints.px_auth_admin = (Get-FileHash -LiteralPath (Join-Path $targetDir 'debug/px_auth_admin.exe')).Hash
    $fingerprints.px_console_admin = (Get-FileHash -LiteralPath (Join-Path $targetDir 'debug/px_console_admin.exe')).Hash
    $deskTool = Join-Path $targetDir 'debug/px_desk.exe'
    $fingerprints.px_desk = (Get-FileHash -LiteralPath $deskTool -Algorithm SHA256).Hash
    Invoke-Checked 'cmd.exe' @('/d','/c','npm.cmd','--prefix',(Join-Path $repo 'web/px_pixels'),'run','build') | Out-Null
    $webUnit = Invoke-Checked 'cmd.exe' @('/d','/c','npm.cmd','--prefix',(Join-Path $repo 'web/px_pixels'),'run','test:unit','--','--run','src/submission.spec.ts')
    if ($webUnit -notmatch '1 passed') { throw 'Desk submission identity unit test missing' }
    Add-Step 'DESK/form-identity: unchanged retry reuses ID; edit and confirmed new submission use new ID'
    Set-LocalEnv 'PIXELS_TEST_CONTAINER' $container
    Invoke-Checked 'cmd.exe' @('/d','/c','npm.cmd','--prefix',(Join-Path $repo 'web/px_auth'),'run','build') | Out-Null
    $authWebUnit = Invoke-Checked 'cmd.exe' @('/d','/c','npm.cmd','--prefix',(Join-Path $repo 'web/px_auth'),'run','test:unit')
    if ($authWebUnit -notmatch 'Tests\s+5 passed') { throw 'Auth frontend contract tests missing' }
    Add-Step 'AUTH-WEB: five contract tests, catalogs, themes, bounds, retry identity and logout failures'
    $authBrowser = Invoke-Checked 'node' @((Join-Path $PSScriptRoot 'auth_browser.cjs'),(Join-Path $targetDir 'debug/px_auth.exe'))
    Write-Host $authBrowser
    foreach ($case in @('auth-browser/login-create-customer','auth-browser/commit-response-loss-exact-retry','auth-browser/renew-and-revoke',
        'auth-browser/language-theme-same-session','auth-browser/operator-create-visitor-denial-logout',
        'auth-process/restart-preserves-session-and-revocation','auth-process/database-outage-fails-closed-and-recovers')) {
        if (-not $authBrowser.Contains("PASS $case")) { throw "Auth functional assertion missing: $case" }
        Add-Step "AUTH/$case"
    }
    $fingerprints.auth_web = @{}
    Get-ChildItem -LiteralPath (Join-Path $repo 'web/px_auth/dist') -File -Recurse | ForEach-Object {
        $fingerprints.auth_web[[IO.Path]::GetRelativePath((Join-Path $repo 'web/px_auth/dist'),$_.FullName)] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    $browserResult = Invoke-Checked 'node' @((Join-Path $PSScriptRoot 'desk_browser.cjs'),$deskTool)
    Write-Host $browserResult
    foreach ($case in @('browser/consult-submit','api/issue-submit-for-admin-browser','browser/login-mark-logout-revokes','process/restart-preserves-data-and-revocation','process/database-outage-503-and-recovery')) {
        if (-not $browserResult.Contains("PASS $case")) { throw "Desk functional assertion missing: $case" }
        Add-Step "DESK/$case"
    }
    $fingerprints.desk_web = @{}
    Get-ChildItem -LiteralPath (Join-Path $repo 'web/px_pixels/dist') -File -Recurse | ForEach-Object {
        $fingerprints.desk_web[[IO.Path]::GetRelativePath((Join-Path $repo 'web/px_pixels/dist'),$_.FullName)] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    if ($Linux) {
        if (-not $IsWindows) { throw '-Linux uses WSL and requires the Windows harness' }
        foreach ($service in @('console','auth','desk')) {
            $baselineDb = "pixels_${service}_linux_baseline"
            Invoke-Checked 'docker' @('exec',$container,'dropdb','--force','-U','pixels_admin',"pixels_$service") | Out-Null
            Invoke-Checked 'docker' @('exec',$container,'createdb','-U','pixels_admin','-O',"pixels_${service}_owner",'-T',$baselineDb,"pixels_$service") | Out-Null
            Invoke-Checked 'docker' @('exec',$container,'psql','-X','-v','ON_ERROR_STOP=1','-U','pixels_admin','-d',"pixels_$service",'-c',
                "REVOKE ALL ON DATABASE pixels_$service FROM PUBLIC; GRANT CONNECT ON DATABASE pixels_$service TO pixels_${service}_owner,pixels_${service}_runtime; REVOKE CREATE ON SCHEMA public FROM PUBLIC") | Out-Null
        }
        Add-Step 'LINUX-RESET: exact production database names rebuilt from pristine isolated baseline'
        $cargoHome = if ($env:CARGO_HOME) { $env:CARGO_HOME } else { Join-Path ([Environment]::GetFolderPath('UserProfile')) '.cargo' }
        Set-LocalEnv 'CARGO_HOME' $cargoHome
        $forward = @('CARGO_HOME/p','SQLX_OFFLINE','SQLX_OFFLINE_DIR/p','PIXELS_PG_ISOLATED_TEST','PIXELS_TEST_CONTAINER','PIXELS_DEPLOYMENT_ID','PIXELS_PG_LOCAL_DEVELOPMENT','PIXELS_TEST_PG_ADMIN_PASSWORD')
        foreach ($service in @('CONSOLE','AUTH','DESK')) {
            foreach ($role in @('OWNER','RUNTIME')) { $forward += "PIXELS_TEST_${service}_${role}_URL" }
        }
        $existing = @($env:WSLENV -split ':' | Where-Object { $_ -and (($_ -split '/')[0] -notin @($forward | ForEach-Object { ($_ -split '/')[0] })) })
        Set-LocalEnv 'WSLENV' (($existing + $forward) -join ':')
        $linuxScript = (Invoke-Checked 'wsl' @('-d','Ubuntu-20.04','--exec','wslpath','-a',(Join-Path $PSScriptRoot 'postgres_wsl.sh'))).Trim()
        $linuxResult = Invoke-Checked 'wsl' @('-d','Ubuntu-20.04','--exec','timeout','--signal=TERM','--kill-after=10s','960','bash','-l',$linuxScript) -TimeoutSeconds 1020
        Write-Host $linuxResult
        # Compare the actual catalogs, not summary counts that can coincide across suites.
        # Sorting retains multiplicities, so a duplicated test cannot replace a missing one.
        $nativeCatalog = @($steps | Where-Object { $_.case -like 'native/*' } |
            ForEach-Object { ($_.case -split '/',3)[2] } | Sort-Object)
        $linuxCatalog = @([regex]::Matches($linuxResult, '(?m)^test ([^\r\n]+?) \.\.\. (ok|FAILED|ignored)\s*$') |
            ForEach-Object { $_.Groups[1].Value } | Sort-Object)
        if (($nativeCatalog -join "`n") -cne ($linuxCatalog -join "`n")) {
            throw 'Windows/Linux native test catalogs differ'
        }
        foreach ($service in @('console','auth','desk')) {
            if ($linuxResult -notmatch "READY service=$service") { throw "Linux schema tool failed for $service" }
        }
        Add-TestCases $linuxResult 'linux' $nativeCatalog.Count
        if ($linuxResult -notmatch '(?m)^([a-f0-9]{64})\s+[^\r\n]+/debug/px_db\s*$') { throw 'Missing Linux schema tool hash' }
        $fingerprints.linux_px_db = $Matches[1]
        if ($linuxResult -notmatch '(?m)^([a-f0-9]{64})\s+[^\r\n]+/debug/px_desk\s*$') { throw 'Missing Linux Desk binary hash' }
        $fingerprints.linux_px_desk = $Matches[1]
        foreach ($tool in @('px_auth','px_auth_admin','px_console_admin','px_cache_probe')) {
            if ($linuxResult -notmatch "(?m)^([a-f0-9]{64})\s+[^\r\n]+/debug/$tool\s*$") { throw "Missing Linux tool hash: $tool" }
            $fingerprints["linux_$tool"] = $Matches[1]
        }
        Add-Step 'LINUX: native WSL Rust unit/integration tests and three schema readiness checks'
    }
    # Cargo integration targets may rebuild this binary with unified test dependency features.
    # Record and verify the exact final binary used by readiness/outage validation, not only the initial tool.
    $fingerprints.px_db = (Get-FileHash -LiteralPath $dbTool -Algorithm SHA256).Hash
    $fingerprints.px_cache_probe = (Get-FileHash -LiteralPath (Join-Path $targetDir 'debug/px_cache_probe.exe') -Algorithm SHA256).Hash
    foreach ($service in @('console','auth','desk')) {
        Use-Service $service 'runtime'
        Invoke-Checked $dbTool @('check',$service) | Out-Null
    }
    Add-Step 'ARTIFACT: final Windows/native schema binary hash captured and all services rechecked'
    Use-Service 'desk' 'runtime'
    Invoke-Compose @('stop','--timeout','10','postgres') | Out-Null
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Invoke-Checked $dbTool @('check','desk') -ExpectFailure | Out-Null
    if ($watch.Elapsed.TotalSeconds -gt 15) { throw 'Database failure exceeded bounded connection timeout' }
    Invoke-Compose @('up','--detach','--wait','--wait-timeout','120') | Out-Null
    Invoke-Checked $dbTool @('check','desk') | Out-Null
    Add-Step 'POOL: outage fails, restart recovers'
    $count = (Invoke-Checked 'docker' @('exec',$container,'psql','-X','-U','pixels_admin','-d','pixels_desk','-Atc','SELECT count(*) FROM pixels.pg_fixture')).Trim()
    if ([int]$count -lt 2) { throw 'Committed test data did not survive PostgreSQL restart' }
    Add-Step 'ENV: committed rows survive restart'
    $tablesByService = @{console=@('users','login_sessions','user_groups','group_members','authorization_outbox','authorization_audit','devices','user_devices','group_device_grants','device_audit','applications','group_app_grants','application_events','guest_sessions','guest_blocks','guest_source_blocks','guest_events','control_runtime','control_runs','nodes','node_audit','application_deployments','deployment_audit','instances','instance_commands','instance_events','instance_admin_actions','rdp_workspaces','workspace_secrets','workspace_audit','resource_sessions','resource_session_events','resource_session_retirements'); auth=@('authors','author_sessions','customers','licenses','license_issuances','license_requests','license_audit'); desk=@('pg_fixture','feedback','versions','admin_sessions')}
    $tablesByService.console += @('file_transfers','file_transfer_events','recordings','recording_events')
    $tablesByService.console += @('saved_connections','saved_connection_events')
    $tablesByService.console += @('cache_roots','cache_runs','cache_runtime','recording_cache','cache_blobs','cache_events')
    $tablesByService.console += @('cache_read_leases')
    $tablesByService.console += @('group_events')
    $tablesByService.console += @('connection_observations','connection_observation_events','update_releases','update_release_events')
    foreach ($service in @('console','auth','desk')) {
        $sourceDb = "pixels_$service"
        $restoreDb = "pixels_test_restore_$service"
        $archive = "/tmp/pixels-test-$service.dump"
        Invoke-Checked 'docker' @('exec',$container,'pg_dump','-U','pixels_admin','-d',$sourceDb,'-Fc','-f',$archive) | Out-Null
        # A new database only. No --clean and no overwrite of the original databases.
        Invoke-Checked 'docker' @('exec',$container,'createdb','-U','pixels_admin',$restoreDb) | Out-Null
        Invoke-Checked 'docker' @('exec',$container,'pg_restore','-U','pixels_admin','-d',$restoreDb,'--exit-on-error',$archive) | Out-Null
        foreach ($table in ($tablesByService[$service] + @('deployment_identity','_sqlx_migrations'))) {
            $digestSql = "SELECT md5(coalesce(string_agg(row_to_json(v)::text,',' ORDER BY row_to_json(v)::text),'')) FROM pixels.$table v"
            $original = (Invoke-Checked 'docker' @('exec',$container,'psql','-X','-U','pixels_admin','-d',$sourceDb,'-Atc',$digestSql)).Trim()
            $restored = (Invoke-Checked 'docker' @('exec',$container,'psql','-X','-U','pixels_admin','-d',$restoreDb,'-Atc',$digestSql)).Trim()
            if ($original -ne $restored) { throw "Restored rows differ: $service/$table" }
        }
        foreach ($definitionSql in @(
            "SELECT c.conname,pg_get_constraintdef(c.oid) FROM pg_constraint c JOIN pg_namespace n ON n.oid=c.connamespace WHERE n.nspname='pixels' ORDER BY c.conname",
            "SELECT indexname,indexdef FROM pg_indexes WHERE schemaname='pixels' ORDER BY indexname"
        )) {
            $original = (Invoke-Checked 'docker' @('exec',$container,'psql','-X','-U','pixels_admin','-d',$sourceDb,'-Atc',$definitionSql)).Trim()
            $restored = (Invoke-Checked 'docker' @('exec',$container,'psql','-X','-U','pixels_admin','-d',$restoreDb,'-Atc',$definitionSql)).Trim()
            if ($original -ne $restored) { throw "Restored constraints or indexes differ: $service" }
        }
        $table = $tablesByService[$service][0]
        $duplicateSql = "INSERT INTO pixels.$table SELECT * FROM pixels.$table LIMIT 1"
        $duplicate = Invoke-Checked 'docker' @('exec',$container,'psql','-X','-v','ON_ERROR_STOP=1','-U','pixels_admin','-d',$restoreDb,'-c',$duplicateSql) -ExpectFailure
        if ($duplicate -notmatch 'duplicate key') { throw 'Restore rejection did not exercise the expected unique constraint' }
        Invoke-Checked 'docker' @('exec',$container,'pg_restore','-U','pixels_admin','-d',$restoreDb,'--exit-on-error','/dev/null') -ExpectFailure | Out-Null
    }
    $orphans = (Invoke-Checked 'docker' @('exec',$container,'psql','-X','-U','pixels_admin','-d','pixels_test_restore_console','-Atc','SELECT count(*) FROM pixels.login_sessions s LEFT JOIN pixels.users u ON u.id=s.user_id WHERE u.id IS NULL')).Trim()
    if ($orphans -ne '0') { throw 'Restored Console contains orphan sessions' }
    Add-Step 'RESTORE-SMOKE: three databases, data/schema/indexes/relationships match, unique constraints and invalid archives reject'
    Assert-SourceHashes
} catch {
    $failed = $true
    $steps.Add([ordered]@{case='execution'; status='FAIL'; reason=$_.Exception.Message})
    Write-Host "FAIL: $($_.Exception.Message)"
} finally {
    if ($Action -in @('Test','TestSuite','PrepareQueries') -and $started) {
        try {
            # This project is generated uniquely by this invocation; never touches the development volume.
            if ($project -notmatch '^pixels-pg-\d{8}-\d{6}-[a-f0-9]{8}$') { throw 'Refusing cleanup of unexpected project' }
            if ($failed) { Invoke-Compose @('logs','--no-color','postgres') | Out-Null }
            Invoke-Compose @('down','--volumes','--timeout','15') | Out-Null
            Add-Step 'CLEANUP: isolated test containers and volume removed'
        } catch {
            $failed = $true
            $steps.Add([ordered]@{case='cleanup'; status='FAIL'; reason=$_.Exception.Message})
        }
    }
    foreach ($entry in $savedEnv.GetEnumerator()) { [Environment]::SetEnvironmentVariable($entry.Key,$entry.Value) }
    if (Test-Path -LiteralPath $reportDir) {
        $scope = if ($Action -eq 'TestSuite') { "Focused native suite: $Suite; no full regression, browser or restore acceptance" } else { 'PG foundation, Console repositories, private cache IO, Auth and Desk products; full DB0-DB5 acceptance not complete' }
        $report = [ordered]@{run_id=$runId; revision=$revision; recorded_at=[DateTime]::UtcNow.ToString('o'); action=$Action; suite=$Suite; linux_requested=$Linux.IsPresent; project=$project; deployment=$secrets.PIXELS_DEPLOYMENT_ID; source_hashes=$sourceHashes; artifacts=$fingerprints; status=$(if($failed){'FAIL'}else{'PASS'}); cases=$steps; scope=$scope}
        [IO.File]::WriteAllText((Join-Path $reportDir 'report.json'),($report | ConvertTo-Json -Depth 8))
        Write-Host "Report: $reportDir"
    }
    # An early return (PrepareQueries/TestSuite) still executes finally but skips
    # statements after it. Cleanup failure must therefore set the process exit here.
    if ($failed) { exit 1 }
}
