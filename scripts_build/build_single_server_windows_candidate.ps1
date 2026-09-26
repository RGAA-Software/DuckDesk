#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$PostgreSqlClientRoot,
    [Parameter(Mandatory)] [string]$OutputDirectory,
    [string]$SuiteVersion = '1.0.4'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$targetDirectory = Join-Path $repositoryRoot '.cache/single-server-windows'
$env:SQLX_OFFLINE = 'true'
$env:CARGO_PROFILE_RELEASE_OPT_LEVEL = '1'
$env:CARGO_PROFILE_RELEASE_INCREMENTAL = 'true'
$env:CARGO_PROFILE_RELEASE_CODEGEN_UNITS = '256'

& cargo.exe build --locked --release --manifest-path (Join-Path $repositoryRoot 'rust_server/Cargo.toml') `
    -p px_console_runtime --bin px_console --bin px_console_admin `
    -p px_pg --bin px_db -p px_relay_server --bin px_relay -p px_backup --bin px_backup `
    --target-dir $targetDirectory
if ($LASTEXITCODE -ne 0) { throw 'Focused Windows Server build failed.' }

& python.exe (Join-Path $repositoryRoot 'scripts/assemble_single_server_windows.py') `
    --bin (Join-Path $targetDirectory 'release') `
    --static (Join-Path $repositoryRoot 'web/px_console/dist') `
    --postgresql-client $PostgreSqlClientRoot `
    --output $OutputDirectory `
    --suite-version $SuiteVersion
if ($LASTEXITCODE -ne 0) { throw 'Windows Server candidate assembly failed.' }
Write-Output "CANDIDATE $OutputDirectory"
