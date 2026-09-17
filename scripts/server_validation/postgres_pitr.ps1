#requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$KeepArtifacts
)

$ErrorActionPreference = 'Stop'
$docker = 'C:\Program Files\Docker\Docker\resources\bin\docker.exe'
if (-not (Test-Path -LiteralPath $docker)) { throw 'Docker Desktop CLI is unavailable' }

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId = 'pitr-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
$image = "pixels-postgres-pitr:$runId"
$network = "pixels-$runId"
$primary = "pixels-$runId-primary"
$restored = "pixels-$runId-restored"
$missingWal = "pixels-$runId-missing-wal"
$volumes = @(
    "pixels-$runId-primary-data",
    "pixels-$runId-repository",
    "pixels-$runId-restored-data",
    "pixels-$runId-corrupt-repository",
    "pixels-$runId-missing-data"
)
$configPath = [IO.Path]::GetFullPath((Join-Path $repo 'deploy/development/postgres-pitr/pgbackrest.conf'))
$dockerfileRoot = [IO.Path]::GetFullPath((Join-Path $repo 'deploy/production/postgres'))
$restorePoint = 'pixels_before_drop_' + [guid]::NewGuid().ToString('N')
$targetWal = ''
$password = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$startedAt = [DateTime]::UtcNow
$steps = [Collections.Generic.List[string]]::new()

function Invoke-Docker([string[]]$Arguments, [switch]$ExpectFailure) {
    $output = & $docker @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($ExpectFailure) {
        if ($exitCode -eq 0) { throw "Docker command unexpectedly succeeded: $($Arguments -join ' ')" }
    } elseif ($exitCode -ne 0) {
        throw "Docker command failed ($exitCode): $($Arguments -join ' ')`n$($output -join "`n")"
    }
    return ($output -join "`n")
}

function Wait-Postgres([string]$Container, [int]$Attempts = 60) {
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        & $docker exec $Container pg_isready -U pixels_admin -d postgres *> $null
        if ($LASTEXITCODE -eq 0) { return }
        Start-Sleep -Milliseconds 500
    }
    $logs = & $docker logs $Container 2>&1
    throw "PostgreSQL did not become ready: $Container`n$($logs -join "`n")"
}

function Start-Postgres([string]$Container, [string]$DataVolume, [string]$RepositoryVolume) {
    Invoke-Docker @(
        'run', '--detach', '--name', $Container, '--network', $network,
        '--env', 'POSTGRES_USER=pixels_admin', '--env', "POSTGRES_PASSWORD=$password",
        '--env', 'POSTGRES_DB=postgres',
        '--volume', "${DataVolume}:/var/lib/postgresql",
        '--volume', "${RepositoryVolume}:/var/lib/pgbackrest",
        '--mount', "type=bind,source=$configPath,target=/etc/pgbackrest/pgbackrest.conf,readonly",
        $image, 'postgres',
        '-c', 'archive_mode=on',
        '-c', 'archive_command=pgbackrest --stanza=pixels archive-push %p',
        '-c', 'archive_timeout=2s',
        '-c', 'log_error_verbosity=terse',
        '-c', 'log_parameter_max_length_on_error=0'
    ) | Out-Null
}

function Restore-Cluster([string]$DataVolume, [string]$RepositoryVolume) {
    Invoke-Docker @(
        'run', '--rm', '--entrypoint', 'sh',
        '--volume', "${DataVolume}:/var/lib/postgresql",
        '--volume', "${RepositoryVolume}:/var/lib/pgbackrest",
        '--mount', "type=bind,source=$configPath,target=/etc/pgbackrest/pgbackrest.conf,readonly",
        $image, '-eu', '-c',
        "install -d -o postgres -g postgres -m 0700 /var/lib/postgresql/18/docker; install -d -o postgres -g postgres -m 0750 /var/log/pgbackrest /var/spool/pgbackrest; exec gosu postgres pgbackrest --stanza=pixels --type=name --target='$restorePoint' --target-action=promote restore"
    ) | Out-Null
}

try {
    Invoke-Docker @('build', '--pull=false', '--tag', $image, $dockerfileRoot) | Out-Null
    $version = Invoke-Docker @('run', '--rm', '--entrypoint', 'pgbackrest', $image, 'version')
    if ($version -notmatch '2\.59\.1') { throw "Unexpected pgBackRest version: $version" }
    $steps.Add('pinned PostgreSQL 18.6 and pgBackRest 2.59.1 image built')

    Invoke-Docker @('network', 'create', $network) | Out-Null
    foreach ($volume in $volumes) { Invoke-Docker @('volume', 'create', $volume) | Out-Null }

    Start-Postgres $primary $volumes[0] $volumes[1]
    Wait-Postgres $primary
    Invoke-Docker @('exec', '--user', 'postgres', $primary, 'pgbackrest', '--stanza=pixels', 'stanza-create') | Out-Null
    Invoke-Docker @('exec', '--user', 'postgres', $primary, 'pgbackrest', '--stanza=pixels', 'check') | Out-Null
    Invoke-Docker @('exec', $primary, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        "CREATE TABLE public.pitr_probe(id integer PRIMARY KEY,label text NOT NULL); INSERT INTO public.pitr_probe VALUES (1,'base');") | Out-Null
    Invoke-Docker @('exec', '--user', 'postgres', $primary, 'pgbackrest', '--stanza=pixels', '--type=full', 'backup') | Out-Null
    $steps.Add('full physical backup completed with continuous WAL archive enabled')

    Invoke-Docker @('exec', $primary, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        'SELECT pg_switch_wal();') | Out-Null
    Invoke-Docker @('exec', '--user', 'postgres', $primary, 'pgbackrest', '--stanza=pixels', 'check') | Out-Null
    Invoke-Docker @('exec', $primary, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        "INSERT INTO public.pitr_probe VALUES (2,'keep');") | Out-Null
    Invoke-Docker @('exec', $primary, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        "SELECT pg_create_restore_point('$restorePoint');") | Out-Null
    $targetWal = (Invoke-Docker @('exec', $primary, 'psql', '-X', '-A', '-t', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        'SELECT pg_walfile_name(pg_current_wal_lsn());')).Trim()
    if ($targetWal -notmatch '^[0-9A-F]{24}$') { throw "Invalid target WAL name: $targetWal" }
    Invoke-Docker @('exec', $primary, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        'DROP TABLE public.pitr_probe;') | Out-Null
    Invoke-Docker @('exec', $primary, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        'SELECT pg_switch_wal();') | Out-Null
    Invoke-Docker @('exec', '--user', 'postgres', $primary, 'pgbackrest', '--stanza=pixels', 'check') | Out-Null
    Invoke-Docker @('stop', '--time', '20', $primary) | Out-Null

    $corruptRepositoryCommand = 'cp -a /source/. /destination/; test -n "$(find /destination/archive -type f -name ''{0}-*'' -print -quit)"; find /destination/archive -type f -name ''{0}-*'' -delete; chown -R postgres:postgres /destination' -f $targetWal
    Invoke-Docker @(
        'run', '--rm', '--entrypoint', 'sh',
        '--volume', "$($volumes[1]):/source:ro",
        '--volume', "$($volumes[3]):/destination",
        $image, '-eu', '-c',
        $corruptRepositoryCommand
    ) | Out-Null

    Restore-Cluster $volumes[2] $volumes[1]
    Start-Postgres $restored $volumes[2] $volumes[1]
    Wait-Postgres $restored
    $rows = Invoke-Docker @('exec', $restored, 'psql', '-X', '-A', '-t', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        "SELECT string_agg(id::text || ':' || label, ',' ORDER BY id) FROM public.pitr_probe;")
    if ($rows.Trim() -ne '1:base,2:keep') { throw "PITR restored unexpected rows: $rows" }
    $steps.Add('named restore point recovered both pre-backup and post-backup committed rows before DROP')
    Invoke-Docker @('stop', '--time', '20', $restored) | Out-Null

    Restore-Cluster $volumes[4] $volumes[3]
    Start-Postgres $missingWal $volumes[4] $volumes[3]
    $unexpectedReady = $false
    for ($attempt = 1; $attempt -le 40; $attempt++) {
        & $docker exec $missingWal pg_isready -U pixels_admin -d postgres *> $null
        if ($LASTEXITCODE -eq 0) {
            $recoveryState = (& $docker exec $missingWal psql -X -A -t -U pixels_admin -d postgres -c 'SELECT pg_is_in_recovery();' 2>$null)
            if ($LASTEXITCODE -eq 0 -and $recoveryState.Trim() -eq 'f') {
                $unexpectedReady = $true
                break
            }
        }
        $running = Invoke-Docker @('inspect', '--format', '{{.State.Running}}', $missingWal)
        if ($running.Trim() -eq 'false') { break }
        Start-Sleep -Milliseconds 500
    }
    if ($unexpectedReady) { throw 'Cluster with missing archived WAL unexpectedly promoted to writable primary' }
    $missingLogs = Invoke-Docker @('logs', $missingWal)
    if ($missingLogs -notmatch 'recovery ended before configured recovery target was reached') {
        throw "Missing-WAL recovery did not fail for the expected reason:`n$missingLogs"
    }
    $steps.Add('missing archived WAL prevented recovery from reaching target and kept PostgreSQL closed')

    $reportDirectory = Join-Path $repo "test-results/server_validation/$runId"
    New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
    $report = [ordered]@{
        run_id = $runId
        status = 'PASS'
        started_at = $startedAt.ToString('o')
        completed_at = [DateTime]::UtcNow.ToString('o')
        postgres = '18.6'
        pgbackrest = '2.59.1'
        restore_target = $restorePoint
        steps = $steps
        scope = 'Single-host Docker functional PITR gate; not independent-host disaster-recovery evidence'
    }
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDirectory 'report.json') -Encoding utf8NoBOM
    Write-Host "PASS PITR: $($steps -join '; ')"
    Write-Host "Report: $reportDirectory"
} finally {
    if (-not $KeepArtifacts) {
        foreach ($container in @($missingWal, $restored, $primary)) {
            & $docker rm --force $container *> $null
        }
        & $docker network rm $network *> $null
        foreach ($volume in $volumes) { & $docker volume rm --force $volume *> $null }
        & $docker image rm --force $image *> $null
    }
}
