#requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$KeepArtifacts
)

$ErrorActionPreference = 'Stop'

$docker = 'C:\Program Files\Docker\Docker\resources\bin\docker.exe'
if (-not (Test-Path -LiteralPath $docker)) {
    throw 'Docker Desktop CLI is unavailable'
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId = 'pitr-object-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$postgresImage = "pixels-postgres-pitr:$runId"
$minioImage = 'quay.io/minio/minio@sha256:14cea493d9a34af32f524e538b8346cf79f3321eff8e708c1e2960462bd8936e'
$minioClientImage = 'quay.io/minio/mc@sha256:a7fe349ef4bd8521fb8497f55c6042871b2ae640607cf99d9bede5e9bdf11727'
$network = "pixels-$runId"
$primaryContainer = "pixels-$runId-primary"
$restoredContainer = "pixels-$runId-restored"
$minioContainer = "pixels-$runId-minio"
$primaryDataVolume = "pixels-$runId-primary-data"
$restoredDataVolume = "pixels-$runId-restored-data"
$wrongKeyDataVolume = "pixels-$runId-wrong-key-data"
$minioDataVolume = "pixels-$runId-minio-data"
$managedVolumes = @($primaryDataVolume, $restoredDataVolume, $wrongKeyDataVolume, $minioDataVolume)
$dockerfileRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'deploy/production/postgres'))
$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) $runId
$configPath = Join-Path $temporaryDirectory 'pgbackrest.conf'
$certificateDirectory = Join-Path $temporaryDirectory 'minio-certs'
$bucketName = 'pixels-pitr'
$minioAccessKey = 'pixels' + [guid]::NewGuid().ToString('N').Substring(0, 16)
$minioSecretKey = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)).ToLowerInvariant()
$repositoryCipherPass = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)).ToLowerInvariant()
$postgresPassword = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)).ToLowerInvariant()
$restorePoint = 'pixels_object_before_drop_' + [guid]::NewGuid().ToString('N')
$startedAt = [DateTime]::UtcNow
$steps = [Collections.Generic.List[string]]::new()

function Invoke-Docker([string[]]$Arguments, [switch]$ExpectFailure) {
    $output = & $docker @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $safeArguments = ($Arguments -join ' ').Replace($minioAccessKey, '<redacted-access-key>').Replace($minioSecretKey, '<redacted-secret>')
    $safeOutput = ($output -join "`n").Replace($minioAccessKey, '<redacted-access-key>').Replace($minioSecretKey, '<redacted-secret>')
    if ($repositoryCipherPass) {
        $safeArguments = $safeArguments.Replace($repositoryCipherPass, '<redacted-cipher-pass>')
        $safeOutput = $safeOutput.Replace($repositoryCipherPass, '<redacted-cipher-pass>')
    }
    if ($postgresPassword) {
        $safeArguments = $safeArguments.Replace($postgresPassword, '<redacted-password>')
        $safeOutput = $safeOutput.Replace($postgresPassword, '<redacted-password>')
    }
    if ($ExpectFailure) {
        if ($exitCode -eq 0) {
            throw "Docker command unexpectedly succeeded: $safeArguments"
        }
    } elseif ($exitCode -ne 0) {
        throw "Docker command failed ($exitCode): $safeArguments`n$safeOutput"
    }
    return $safeOutput
}

function Get-RepositoryEnvironment([string]$CipherPass = $repositoryCipherPass) {
    return @(
        '--env', "PGBACKREST_REPO1_S3_KEY=$minioAccessKey",
        '--env', "PGBACKREST_REPO1_S3_KEY_SECRET=$minioSecretKey",
        '--env', "PGBACKREST_REPO1_CIPHER_PASS=$CipherPass"
    )
}

function Wait-Postgres([string]$Container, [int]$Attempts = 60) {
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        & $docker exec $Container pg_isready -U pixels_admin -d postgres *> $null
        if ($LASTEXITCODE -eq 0) {
            return
        }
        Start-Sleep -Milliseconds 500
    }
    $logs = & $docker logs $Container 2>&1
    throw "PostgreSQL did not become ready: $Container`n$($logs -join "`n")"
}

function Initialize-ObjectBucket {
    for ($attempt = 1; $attempt -le 60; $attempt++) {
        & $docker run --rm --network $network --entrypoint /bin/sh $minioClientImage -eu -c `
            "mc --insecure alias set pixels https://${minioContainer}:9000 '$minioAccessKey' '$minioSecretKey' >/dev/null && mc --insecure mb pixels/$bucketName >/dev/null" `
            *> $null
        if ($LASTEXITCODE -eq 0) {
            return
        }
        Start-Sleep -Milliseconds 500
    }
    $logs = & $docker logs $minioContainer 2>&1
    throw "MinIO bucket did not become ready`n$($logs -join "`n")"
}

function Start-Postgres([string]$Container, [string]$DataVolume) {
    $repositoryEnvironment = Get-RepositoryEnvironment
    $arguments = @(
        'run', '--detach', '--name', $Container, '--network', $network,
        '--env', 'POSTGRES_USER=pixels_admin', '--env', "POSTGRES_PASSWORD=$postgresPassword",
        '--env', 'POSTGRES_DB=postgres'
    ) + $repositoryEnvironment + @(
        '--volume', "${DataVolume}:/var/lib/postgresql",
        '--mount', "type=bind,source=$configPath,target=/etc/pgbackrest/pgbackrest.conf,readonly",
        $postgresImage, 'postgres',
        '-c', 'archive_mode=on',
        '-c', 'archive_command=pgbackrest --stanza=pixels archive-push %p',
        '-c', 'archive_timeout=2s',
        '-c', 'log_error_verbosity=terse',
        '-c', 'log_parameter_max_length_on_error=0'
    )
    Invoke-Docker -Arguments $arguments | Out-Null
}

function Invoke-PgBackRest([string]$Container, [string[]]$Arguments) {
    $repositoryEnvironment = Get-RepositoryEnvironment
    Invoke-Docker (@('exec') + $repositoryEnvironment + @('--user', 'postgres', $Container, 'pgbackrest') + $Arguments) | Out-Null
}

function Restore-Cluster([string]$DataVolume, [string]$CipherPass, [switch]$ExpectFailure) {
    $repositoryEnvironment = Get-RepositoryEnvironment -CipherPass $CipherPass
    $arguments = @(
        'run', '--rm', '--network', $network, '--entrypoint', 'sh'
    ) + $repositoryEnvironment + @(
        '--volume', "${DataVolume}:/var/lib/postgresql",
        '--mount', "type=bind,source=$configPath,target=/etc/pgbackrest/pgbackrest.conf,readonly",
        $postgresImage, '-eu', '-c',
        "install -d -o postgres -g postgres -m 0700 /var/lib/postgresql/18/docker; install -d -o postgres -g postgres -m 0750 /var/log/pgbackrest /var/spool/pgbackrest; exec gosu postgres pgbackrest --stanza=pixels --type=name --target='$restorePoint' --target-action=promote restore"
    )
    Invoke-Docker -Arguments $arguments -ExpectFailure:$ExpectFailure | Out-Null
}

try {
    New-Item -ItemType Directory -Path $certificateDirectory -Force | Out-Null
    @"
[pixels]
pg1-path=/var/lib/postgresql/18/docker
pg1-user=pixels_admin

[global]
repo1-type=s3
repo1-path=/pixels
repo1-s3-bucket=$bucketName
repo1-s3-endpoint=$minioContainer
repo1-s3-region=us-east-1
repo1-s3-key-type=shared
repo1-s3-uri-style=path
repo1-storage-port=9000
repo1-storage-verify-tls=n
repo1-cipher-type=aes-256-cbc
repo1-retention-full=2
repo1-retention-diff=2
repo1-retention-history=30
start-fast=y
process-max=2
log-level-console=info
spool-path=/var/spool/pgbackrest

[global:archive-push]
compress-level=3
"@ | Set-Content -LiteralPath $configPath -Encoding utf8NoBOM

    Invoke-Docker @('build', '--pull=false', '--tag', $postgresImage, $dockerfileRoot) | Out-Null
    $pgBackRestVersion = Invoke-Docker @('run', '--rm', '--entrypoint', 'pgbackrest', $postgresImage, 'version')
    if ($pgBackRestVersion -notmatch '2\.59\.1') {
        throw "Unexpected pgBackRest version: $pgBackRestVersion"
    }
    $steps.Add('pinned PostgreSQL 18.6 and pgBackRest 2.59.1 image built')

    Invoke-Docker @(
        'run', '--rm', '--entrypoint', 'sh',
        '--mount', "type=bind,source=$certificateDirectory,target=/certificates",
        $postgresImage, '-eu', '-c',
        "openssl req -x509 -nodes -newkey rsa:2048 -keyout /certificates/private.key -out /certificates/public.crt -days 1 -subj '/CN=$minioContainer' -addext 'subjectAltName=DNS:$minioContainer' >/dev/null 2>&1"
    ) | Out-Null

    Invoke-Docker @('network', 'create', $network) | Out-Null
    foreach ($managedVolume in $managedVolumes) {
        Invoke-Docker @('volume', 'create', $managedVolume) | Out-Null
    }
    Invoke-Docker @(
        'run', '--detach', '--name', $minioContainer, '--network', $network,
        '--env', "MINIO_ROOT_USER=$minioAccessKey", '--env', "MINIO_ROOT_PASSWORD=$minioSecretKey",
        '--volume', "${minioDataVolume}:/data",
        '--mount', "type=bind,source=$certificateDirectory,target=/root/.minio/certs,readonly",
        $minioImage, 'server', '/data', '--address', ':9000'
    ) | Out-Null
    Initialize-ObjectBucket
    $steps.Add('digest-pinned MinIO S3 bucket created in an isolated Docker volume')

    Start-Postgres -Container $primaryContainer -DataVolume $primaryDataVolume
    Wait-Postgres -Container $primaryContainer
    Invoke-PgBackRest -Container $primaryContainer -Arguments @('--stanza=pixels', 'stanza-create')
    Invoke-PgBackRest -Container $primaryContainer -Arguments @('--stanza=pixels', 'check')
    Invoke-Docker @(
        'exec', $primaryContainer, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        "CREATE TABLE public.object_pitr_probe(id integer PRIMARY KEY,label text NOT NULL); INSERT INTO public.object_pitr_probe VALUES (1,'base');"
    ) | Out-Null
    Invoke-PgBackRest -Container $primaryContainer -Arguments @('--stanza=pixels', '--type=full', 'backup')
    $steps.Add('encrypted full backup completed through the S3-compatible API')

    Invoke-Docker @(
        'exec', $primaryContainer, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        "INSERT INTO public.object_pitr_probe VALUES (2,'keep');"
    ) | Out-Null
    Invoke-Docker @(
        'exec', $primaryContainer, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        "SELECT pg_create_restore_point('$restorePoint'); SELECT pg_switch_wal();"
    ) | Out-Null
    Invoke-PgBackRest -Container $primaryContainer -Arguments @('--stanza=pixels', 'check')
    Invoke-Docker @(
        'exec', $primaryContainer, 'psql', '-X', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        'DROP TABLE public.object_pitr_probe; SELECT pg_switch_wal();'
    ) | Out-Null
    Invoke-PgBackRest -Container $primaryContainer -Arguments @('--stanza=pixels', 'check')
    Invoke-Docker @('stop', '--time', '20', $primaryContainer) | Out-Null
    Invoke-Docker @('rm', $primaryContainer) | Out-Null
    Invoke-Docker @('volume', 'rm', $primaryDataVolume) | Out-Null
    $steps.Add('primary container and database volume removed before recovery')

    $wrongCipherPass = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)).ToLowerInvariant()
    Restore-Cluster -DataVolume $wrongKeyDataVolume -CipherPass $wrongCipherPass -ExpectFailure
    $steps.Add('restore with the wrong client-side encryption passphrase failed closed')

    Restore-Cluster -DataVolume $restoredDataVolume -CipherPass $repositoryCipherPass
    Start-Postgres -Container $restoredContainer -DataVolume $restoredDataVolume
    Wait-Postgres -Container $restoredContainer
    $restoredRows = Invoke-Docker @(
        'exec', $restoredContainer, 'psql', '-X', '-A', '-t', '-v', 'ON_ERROR_STOP=1', '-U', 'pixels_admin', '-d', 'postgres', '-c',
        "SELECT string_agg(id::text || ':' || label, ',' ORDER BY id) FROM public.object_pitr_probe;"
    )
    if ($restoredRows.Trim() -ne '1:base,2:keep') {
        throw "Object-store PITR restored unexpected rows: $restoredRows"
    }
    $steps.Add('named-point recovery from object storage restored committed rows before DROP')

    $objectListing = Invoke-Docker @(
        'run', '--rm', '--network', $network, '--entrypoint', '/bin/sh', $minioClientImage, '-eu', '-c',
        "mc --insecure alias set pixels https://${minioContainer}:9000 '$minioAccessKey' '$minioSecretKey' >/dev/null && mc --insecure ls --recursive pixels/$bucketName"
    )
    $objectCount = @($objectListing -split "`n" | Where-Object { $_.Trim().Length -gt 0 }).Count
    if ($objectCount -lt 1) {
        throw 'Object repository contains no backup objects'
    }

    $reportDirectory = Join-Path $repositoryRoot "test-results/server_validation/$runId"
    New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
    $report = [ordered]@{
        run_id = $runId
        status = 'PASS'
        started_at = $startedAt.ToString('o')
        completed_at = [DateTime]::UtcNow.ToString('o')
        postgres = '18.6'
        pgbackrest = '2.59.1'
        minio_image = $minioImage
        minio_client_image = $minioClientImage
        object_count = $objectCount
        client_side_encryption = 'aes-256-cbc'
        restore_target = $restorePoint
        steps = $steps
        scope = 'Single-host isolated Docker validation of the S3 protocol, client-side encryption, source-loss recovery, and PITR. MinIO used ephemeral self-signed TLS with verification disabled inside the private test network; this is not independent-fault-domain or production certificate-verification evidence.'
    }
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDirectory 'report.json') -Encoding utf8NoBOM
    Write-Host "PASS object-store PITR: $($steps -join '; ')"
    Write-Host "Report: $reportDirectory"
} finally {
    if (-not $KeepArtifacts) {
        foreach ($managedContainer in @($restoredContainer, $primaryContainer, $minioContainer)) {
            & $docker rm --force $managedContainer *> $null
        }
        & $docker network rm $network *> $null
        foreach ($managedVolume in $managedVolumes) {
            & $docker volume rm --force $managedVolume *> $null
        }
        & $docker image rm --force $postgresImage *> $null
        if (Test-Path -LiteralPath $temporaryDirectory) {
            Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
        }
    }
}
