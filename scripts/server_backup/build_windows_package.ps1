#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$BackupBinary,
    [Parameter(Mandatory)]
    [string]$PostgreSqlArchive,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repositoryRoot 'output/px_backup/releases'
}
$resolvedOutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$resolvedBinary = (Resolve-Path -LiteralPath $BackupBinary).Path
$resolvedArchive = (Resolve-Path -LiteralPath $PostgreSqlArchive).Path
if (-not [IO.File]::Exists($resolvedBinary) -or -not [IO.File]::Exists($resolvedArchive)) {
    throw 'Backup binary and PostgreSQL archive must both be regular files.'
}

$clientManifestPath = Join-Path $repositoryRoot 'deploy/production/windows-backup/postgresql-client-18.6.json'
$clientManifest = Get-Content -LiteralPath $clientManifestPath -Raw | ConvertFrom-Json
if ($clientManifest.schema_version -ne 1 -or $clientManifest.product -ne 'postgresql-client-windows-x64' -or
    $clientManifest.postgresql_version -ne '18.6' -or $clientManifest.files.Count -ne 14) {
    throw 'PostgreSQL client manifest is invalid.'
}
$archiveHash = (Get-FileHash -LiteralPath $resolvedArchive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($archiveHash -cne [string]$clientManifest.archive_sha256) {
    throw 'PostgreSQL archive hash does not match the reviewed release.'
}

$workspaceManifest = Join-Path $repositoryRoot 'rust_server/Cargo.toml'
$cargoMetadataJson = & cargo.exe metadata --offline --locked --format-version=1 --no-deps --manifest-path $workspaceManifest 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Unable to resolve the locked px_backup version: $($cargoMetadataJson -join ' ')"
}
$backupPackages = @($cargoMetadataJson | ConvertFrom-Json | Select-Object -ExpandProperty packages | Where-Object name -eq 'px_backup')
if ($backupPackages.Count -ne 1 -or [string]$backupPackages[0].version -cnotmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') {
    throw 'The locked workspace does not contain one versioned px_backup package.'
}
$backupVersion = [string]$backupPackages[0].version
$binaryHash = (Get-FileHash -LiteralPath $resolvedBinary -Algorithm SHA256).Hash.ToLowerInvariant()
$packageId = "$backupVersion-pg18.6-r$($clientManifest.archive_revision)-$($binaryHash.Substring(0, 12))"
if ($packageId -notmatch '^[a-zA-Z0-9.-]{1,96}$') {
    throw 'Generated package identity is invalid.'
}
New-Item -ItemType Directory -Force -Path $resolvedOutputRoot | Out-Null
$releasePath = Join-Path $resolvedOutputRoot $packageId
if (Test-Path -LiteralPath $releasePath) {
    throw "Release already exists: $releasePath"
}
$stagingPath = Join-Path $resolvedOutputRoot ".staging-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stagingPath | Out-Null

function Get-LowerHash {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

try {
    $postgresqlRoot = Join-Path $stagingPath 'postgresql'
    New-Item -ItemType Directory -Path (Join-Path $postgresqlRoot 'bin') -Force | Out-Null
    Add-Type -AssemblyName System.IO.Compression
    $archive = [IO.Compression.ZipFile]::OpenRead($resolvedArchive)
    try {
        $entriesByName = @{}
        foreach ($entry in $archive.Entries) {
            if ($entriesByName.ContainsKey($entry.FullName)) {
                throw "PostgreSQL archive contains a duplicate entry: $($entry.FullName)"
            }
            $entriesByName[$entry.FullName] = $entry
        }
        foreach ($expectedFile in $clientManifest.files) {
            $relativePath = [string]$expectedFile.path
            if ($relativePath -notmatch '^(bin/[A-Za-z0-9_.-]+|[A-Za-z0-9_.-]+)$') {
                throw "Unsafe PostgreSQL package path: $relativePath"
            }
            $archivePath = "pgsql/$relativePath"
            if (-not $entriesByName.ContainsKey($archivePath)) {
                throw "PostgreSQL archive is missing $archivePath"
            }
            $destinationPath = Join-Path $postgresqlRoot $relativePath
            $destinationParent = Split-Path -Parent $destinationPath
            New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
            $inputStream = $entriesByName[$archivePath].Open()
            $outputStream = [IO.File]::Open($destinationPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try {
                $inputStream.CopyTo($outputStream)
            } finally {
                $outputStream.Dispose()
                $inputStream.Dispose()
            }
            $fileInfo = Get-Item -LiteralPath $destinationPath
            if ($fileInfo.Length -ne [long]$expectedFile.size -or
                (Get-LowerHash -Path $destinationPath) -cne [string]$expectedFile.sha256) {
                throw "PostgreSQL client file identity mismatch: $relativePath"
            }
        }
    } finally {
        $archive.Dispose()
    }

    Copy-Item -LiteralPath $resolvedBinary -Destination (Join-Path $stagingPath 'px_backup.exe')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install_windows_service.ps1') -Destination (Join-Path $stagingPath 'install.ps1')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'uninstall_windows_service.ps1') -Destination (Join-Path $stagingPath 'uninstall.ps1')

    foreach ($toolName in @('pg_dump.exe', 'pg_restore.exe', 'psql.exe', 'createdb.exe')) {
        $toolPath = Join-Path $postgresqlRoot "bin/$toolName"
        $versionOutput = & $toolPath '--version' 2>&1
        if ($LASTEXITCODE -ne 0 -or ($versionOutput -join "`n") -notmatch 'PostgreSQL\) 18\.6$') {
            throw "PostgreSQL tool failed its version probe: $toolName"
        }
    }

    $packageFiles = Get-ChildItem -LiteralPath $stagingPath -File -Recurse | ForEach-Object {
        [ordered]@{
            path = [IO.Path]::GetRelativePath($stagingPath, $_.FullName).Replace('\', '/')
            size = $_.Length
            sha256 = Get-LowerHash -Path $_.FullName
        }
    } | Sort-Object path
    $packageManifest = [ordered]@{
        schema_version = 1
        product = 'pixels-backup-windows-x64'
        package_id = $packageId
        backup_version = $backupVersion
        postgresql_version = '18.6'
        postgresql_archive_sha256 = $archiveHash
        files = @($packageFiles)
    }
    [IO.File]::WriteAllText(
        (Join-Path $stagingPath 'package-manifest.json'),
        ($packageManifest | ConvertTo-Json -Depth 5 -Compress),
        [Text.UTF8Encoding]::new($false)
    )
    Move-Item -LiteralPath $stagingPath -Destination $releasePath
    $packageManifestHash = Get-LowerHash -Path (Join-Path $releasePath 'package-manifest.json')
    Write-Output "PACKAGE $releasePath"
    Write-Output "PACKAGE_MANIFEST_SHA256 $packageManifestHash"
} catch {
    if (Test-Path -LiteralPath $stagingPath) {
        $resolvedStagingPath = [IO.Path]::GetFullPath($stagingPath)
        if ([IO.Path]::GetDirectoryName($resolvedStagingPath) -cne $resolvedOutputRoot) {
            throw 'Refusing unsafe package staging cleanup.'
        }
        Remove-Item -LiteralPath $resolvedStagingPath -Recurse -Force
    }
    throw
}
