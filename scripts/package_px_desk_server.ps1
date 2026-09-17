#requires -Version 7.0
# Release-only entry: complete Desk web/server/schema tool, independent Desk version bump.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspace = Join-Path $repo 'rust_server'
function Invoke-Build([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Desk package build failed: $Executable" }
}
Push-Location $repo
try {
    Invoke-Build 'python' @((Join-Path $workspace 'set_server_version.py'),'px_desk_server','--bump')
    Invoke-Build 'npm.cmd' @('--prefix',(Join-Path $repo 'web/px_desk'),'ci')
    Invoke-Build 'npm.cmd' @('--prefix',(Join-Path $repo 'web/px_desk'),'run','build')
    Invoke-Build 'cargo' @('build','--locked','--release','--manifest-path',(Join-Path $workspace 'Cargo.toml'),'-p','px_desk_server','-p','px_pg','--target-dir',(Join-Path $repo '.cache/desk-release'))
    # New directory only: packaging never recursively deletes an existing deployment or credentials.
    $releaseRoot = Join-Path $repo 'output/px_desk/releases'
    $release = Join-Path $releaseRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $release | Out-Null
    foreach ($name in @('px_desk.exe','px_db.exe')) {
        $source = Join-Path $repo ".cache/desk-release/release/$name"
        $destination = Join-Path $release $name
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) { throw "Artifact mismatch: $name" }
    }
    $web = Join-Path $repo 'web/px_desk/dist'
    $webTarget = Join-Path $release 'static'
    Copy-Item -LiteralPath $web -Destination $webTarget -Recurse
    foreach ($source in Get-ChildItem -LiteralPath $web -File -Recurse) {
        $destination = Join-Path $webTarget ([IO.Path]::GetRelativePath($web,$source.FullName))
        if ((Get-FileHash -LiteralPath $source.FullName).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) { throw 'Web artifact mismatch' }
    }
    Copy-Item -LiteralPath (Join-Path $repo 'docs/px_desk_web_overview.md'),(Join-Path $repo 'docs/postgresql_desk_contract.md') -Destination $release
    $hashes = @{}
    Get-ChildItem -LiteralPath $release -File -Recurse | ForEach-Object {
        $hashes[[IO.Path]::GetRelativePath($release,$_.FullName)] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    [IO.File]::WriteAllText((Join-Path $release 'sha256.json'),($hashes | ConvertTo-Json))
    Write-Host "Desk release: $release"
    Write-Host 'Configure PostgreSQL/deployment/admin/TLS explicitly; initialize with px_db using owner, then run px_desk using runtime.'
} finally { Pop-Location }
