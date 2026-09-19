#requires -Version 7.0
# Release-only entry: complete Console web/server/schema tools, independent Console version bump.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$serverWorkspace = Join-Path $repositoryRoot 'rust_server'
$releaseTarget = Join-Path $repositoryRoot '.cache/console-release'
$consoleManifest = Join-Path $serverWorkspace 'px_console_server/runtime/Cargo.toml'

function Invoke-Build([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Console package build failed: $Executable"
    }
}

function Copy-VerifiedFile([string]$Source, [string]$Destination) {
    Copy-Item -LiteralPath $Source -Destination $Destination
    if ((Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash) {
        throw "Artifact mismatch: $([IO.Path]::GetFileName($Source))"
    }
}

$previousOffline = $env:SQLX_OFFLINE
$env:SQLX_OFFLINE = 'true'
Push-Location $repositoryRoot
try {
    Invoke-Build 'python' @((Join-Path $serverWorkspace 'set_server_version.py'), 'px_console_server', '--bump')
    Invoke-Build 'npm.cmd' @('--prefix', (Join-Path $repositoryRoot 'web/px_console'), 'ci')
    Invoke-Build 'npm.cmd' @('--prefix', (Join-Path $repositoryRoot 'web/px_console'), 'run', 'test:unit', '--', '--run')
    Invoke-Build 'npm.cmd' @('--prefix', (Join-Path $repositoryRoot 'web/px_console'), 'run', 'build')
    Invoke-Build 'cargo' @(
        'build', '--locked', '--release', '--manifest-path', (Join-Path $serverWorkspace 'Cargo.toml'),
        '-p', 'px_console_runtime', '--bins', '-p', 'px_pg', '--target-dir', $releaseTarget
    )

    $versionLine = Get-Content -LiteralPath $consoleManifest | Where-Object { $_ -match '^version\s*=' } | Select-Object -First 1
    if ($versionLine -notmatch '^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"') {
        throw 'Unable to read the Console release version.'
    }
    $version = $Matches[1]
    $releaseRoot = Join-Path $repositoryRoot 'output/px_console/releases'
    $releaseDirectory = Join-Path $releaseRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $releaseDirectory | Out-Null

    foreach ($executableName in @('px_console.exe', 'px_console_admin.exe', 'px_db.exe')) {
        Copy-VerifiedFile (Join-Path $releaseTarget "release/$executableName") (Join-Path $releaseDirectory $executableName)
    }

    $webSource = Join-Path $repositoryRoot 'web/px_console/dist'
    $webDestination = Join-Path $releaseDirectory 'static'
    Copy-Item -LiteralPath $webSource -Destination $webDestination -Recurse
    foreach ($sourceFile in Get-ChildItem -LiteralPath $webSource -File -Recurse) {
        $relativePath = [IO.Path]::GetRelativePath($webSource, $sourceFile.FullName)
        $destinationFile = Join-Path $webDestination $relativePath
        if ((Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash) {
            throw "Web artifact mismatch: $relativePath"
        }
    }

    foreach ($documentName in @(
        'px_console_server_runtime_config.md',
        'postgresql_console_runtime_contract.md',
        'postgresql_resource_session_contract.md'
    )) {
        Copy-VerifiedFile (Join-Path $repositoryRoot "docs/$documentName") (Join-Path $releaseDirectory $documentName)
    }
    $systemdDirectory = Join-Path $releaseDirectory 'deploy/systemd'
    New-Item -ItemType Directory -Path $systemdDirectory | Out-Null
    Copy-VerifiedFile (Join-Path $repositoryRoot 'deploy/systemd/pixels-console@.service') `
        (Join-Path $systemdDirectory 'pixels-console@.service')

    $releaseMetadata = [ordered]@{
        product = 'px_console'
        package = 'px_console_runtime'
        version = $version
        database = 'postgresql'
        configuration = 'environment'
        supervision = @('systemd')
        includes_secrets = $false
    }
    [IO.File]::WriteAllText(
        (Join-Path $releaseDirectory 'release.json'),
        (($releaseMetadata | ConvertTo-Json) + "`n"),
        [Text.UTF8Encoding]::new($false)
    )

    $forbiddenNames = @('px_console.toml', 'px_media.exe', 'px_turn.exe', 'turnserver.conf', 'config.ini')
    foreach ($forbiddenName in $forbiddenNames) {
        if (Get-ChildItem -LiteralPath $releaseDirectory -Recurse -Force | Where-Object Name -eq $forbiddenName) {
            throw "Retired artifact entered the Console release: $forbiddenName"
        }
    }

    $hashes = [ordered]@{}
    foreach ($releaseFile in Get-ChildItem -LiteralPath $releaseDirectory -File -Recurse | Sort-Object FullName) {
        $relativePath = [IO.Path]::GetRelativePath($releaseDirectory, $releaseFile.FullName).Replace('\', '/')
        $hashes[$relativePath] = (Get-FileHash -LiteralPath $releaseFile.FullName -Algorithm SHA256).Hash
    }
    [IO.File]::WriteAllText(
        (Join-Path $releaseDirectory 'sha256.json'),
        (($hashes | ConvertTo-Json) + "`n"),
        [Text.UTF8Encoding]::new($false)
    )
    Write-Host "Console release: $releaseDirectory"
    Write-Host 'Configure PostgreSQL, deployment identity, private keys, TLS and public origin explicitly before startup.'
} finally {
    Pop-Location
    $env:SQLX_OFFLINE = $previousOffline
}
