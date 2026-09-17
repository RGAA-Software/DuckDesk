#requires -Version 7.0
# Release-only entry: complete Auth web/server/schema tool, independent Auth version bump.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspace = Join-Path $repo 'rust_server'
function Invoke-Build([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Auth package build failed: $Executable" }
}
$previousOffline = $env:SQLX_OFFLINE
$env:SQLX_OFFLINE = 'true'
Push-Location $repo
try {
    Invoke-Build 'python' @((Join-Path $workspace 'set_server_version.py'),'px_auth_server','--bump')
    Invoke-Build 'npm.cmd' @('--prefix',(Join-Path $repo 'web/px_auth'),'ci')
    Invoke-Build 'npm.cmd' @('--prefix',(Join-Path $repo 'web/px_auth'),'run','build')
    Invoke-Build 'cargo' @('build','--locked','--release','--manifest-path',(Join-Path $workspace 'Cargo.toml'),'-p','px_auth_server','-p','px_pg','--target-dir',(Join-Path $repo '.cache/auth-release'))
    # New directory only: packaging never recursively deletes an existing deployment or credentials.
    $releaseRoot = Join-Path $repo 'output/px_auth/releases'
    $release = Join-Path $releaseRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $release | Out-Null
    foreach ($name in @('px_auth.exe','px_auth_admin.exe','px_db.exe')) {
        $source = Join-Path $repo ".cache/auth-release/release/$name"
        $destination = Join-Path $release $name
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) { throw "Artifact mismatch: $name" }
    }
    $web = Join-Path $repo 'web/px_auth/dist'
    $webTarget = Join-Path $release 'static'
    Copy-Item -LiteralPath $web -Destination $webTarget -Recurse
    foreach ($source in Get-ChildItem -LiteralPath $web -File -Recurse) {
        $destination = Join-Path $webTarget ([IO.Path]::GetRelativePath($web,$source.FullName))
        if ((Get-FileHash -LiteralPath $source.FullName).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) { throw 'Web artifact mismatch' }
    }
    Copy-Item -LiteralPath (Join-Path $repo 'docs/px_auth_server_runtime_config.md'),(Join-Path $repo 'docs/postgresql_license_contract.md') -Destination $release
    $hashes = @{}
    Get-ChildItem -LiteralPath $release -File -Recurse | ForEach-Object {
        $hashes[[IO.Path]::GetRelativePath($release,$_.FullName)] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    [IO.File]::WriteAllText((Join-Path $release 'sha256.json'),($hashes | ConvertTo-Json))
    Write-Host "Auth release: $release"
    Write-Host 'Configure PostgreSQL/deployment/signing key/TLS explicitly; initialize schema and administrator with owner, then run px_auth using runtime.'
} finally { Pop-Location; $env:SQLX_OFFLINE = $previousOffline }
