#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$PostgreSqlClientRoot,
    [Parameter(Mandatory)] [string]$OutputDirectory,
    [Parameter(Mandatory)] [string]$SuiteVersion
)

$ErrorActionPreference = 'Stop'
if ($SuiteVersion -cnotmatch '^\d+\.\d+\.\d+$') { throw 'Server suite version is invalid.' }
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $resolvedOutput) { throw "Release output already exists: $resolvedOutput" }
$targetDirectory = Join-Path $repositoryRoot '.cache/single-server-windows-release'
$env:SQLX_OFFLINE = 'true'
$env:CARGO_PROFILE_RELEASE_OPT_LEVEL = '3'
$env:CARGO_PROFILE_RELEASE_LTO = 'fat'
$env:CARGO_PROFILE_RELEASE_CODEGEN_UNITS = '1'
$env:CARGO_PROFILE_RELEASE_INCREMENTAL = 'false'

& npm.cmd --prefix (Join-Path $repositoryRoot 'web/px_console') ci --no-audit --no-fund
if ($LASTEXITCODE -ne 0) { throw 'Console Web dependency installation failed.' }
& npm.cmd --prefix (Join-Path $repositoryRoot 'web/px_console') run build
if ($LASTEXITCODE -ne 0) { throw 'Console Web build failed.' }
& cargo.exe build --locked --release --manifest-path (Join-Path $repositoryRoot 'rust_server/Cargo.toml') `
    -p px_console_runtime --bin px_console --bin px_console_admin `
    -p px_pg --bin px_db -p px_relay_server --bin px_relay -p px_backup --bin px_backup `
    --target-dir $targetDirectory
if ($LASTEXITCODE -ne 0) { throw 'Optimized Windows Server build failed.' }

New-Item -ItemType Directory -Path $resolvedOutput -ErrorAction Stop | Out-Null
$packageDirectory = Join-Path $resolvedOutput 'package'
& python.exe (Join-Path $repositoryRoot 'scripts/assemble_single_server_windows.py') `
    --bin (Join-Path $targetDirectory 'release') `
    --static (Join-Path $repositoryRoot 'web/px_console/dist') `
    --postgresql-client $PostgreSqlClientRoot `
    --output $packageDirectory `
    --suite-version $SuiteVersion `
    --build-profile optimized-release
if ($LASTEXITCODE -ne 0) { throw 'Optimized Windows Server package assembly failed.' }

$setupPath = Join-Path $resolvedOutput "PixelsServer_${SuiteVersion}_Setup.exe"
& python.exe (Join-Path $repositoryRoot 'setup/make_single_server.py') --package $packageDirectory --output $setupPath
if ($LASTEXITCODE -ne 0) { throw 'Windows Server Setup build failed.' }
Write-Output "Windows Customer Server release: $setupPath"
