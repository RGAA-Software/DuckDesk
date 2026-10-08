#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$PostgreSqlClientRoot,
    [string]$SuiteVersion
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build_official/private_server/candidates'))
$outputRoot = Join-Path $candidateRoot 'windows-single-server'
$stagingRoot = Join-Path $candidateRoot ".windows-single-server-staging-$([Guid]::NewGuid().ToString('N'))"
$previousRoot = Join-Path $candidateRoot ".windows-single-server-previous-$([Guid]::NewGuid().ToString('N'))"
foreach ($targetPath in @($outputRoot, $stagingRoot, $previousRoot)) {
    if (-not [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($targetPath)).Equals(
            $candidateRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Windows Server candidate output must stay in the product candidates directory.'
    }
}
if (-not $SuiteVersion) {
    $SuiteVersion = (Get-Content -LiteralPath (Join-Path $repositoryRoot 'scripts/server_private/server_suite_version.json') `
        -Raw | ConvertFrom-Json).next_version
}
if ($SuiteVersion -cnotmatch '^\d+\.\d+\.\d+$') { throw 'Server candidate version is invalid.' }
if (Test-Path -LiteralPath $outputRoot) {
    if (((Get-Item -LiteralPath $outputRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Windows Server candidate output cannot be a reparse point.'
    }
    $publishedManifestPath = Join-Path $outputRoot 'package/sha256.json'
    if (-not (Test-Path -LiteralPath $publishedManifestPath -PathType Leaf)) {
        throw 'Existing Windows Server candidate does not contain a package manifest.'
    }
    $publishedManifest = Get-Content -LiteralPath $publishedManifestPath -Raw | ConvertFrom-Json
    if ($publishedManifest.product -cne 'pixels-single-server' -or
        $publishedManifest.distribution -cne 'official' -or
        $publishedManifest.platform -cne 'windows-x86_64' -or
        [string]$publishedManifest.suite_version -cnotmatch '^\d+\.\d+\.\d+$') {
        throw 'Existing Windows Server candidate has another product identity.'
    }
    $previousSetupName = "PixelsServer_$($publishedManifest.suite_version)_Setup.exe"
    $expectedEntries = @('package', $previousSetupName, "$previousSetupName.sha256")
    $actualEntries = @(Get-ChildItem -LiteralPath $outputRoot -Force | Select-Object -ExpandProperty Name)
    if ((@($actualEntries | Sort-Object) -join "`n") -cne (@($expectedEntries | Sort-Object) -join "`n")) {
        throw 'Existing Windows Server candidate contains unrecognized files; refusing replacement.'
    }
}
if ((Test-Path -LiteralPath $stagingRoot) -or (Test-Path -LiteralPath $previousRoot)) {
    throw 'Windows Server candidate staging path already exists.'
}
$targetDirectory = Join-Path $repositoryRoot '.cache/single-server-windows'
$env:SQLX_OFFLINE = 'true'
$env:CARGO_PROFILE_RELEASE_OPT_LEVEL = '1'
$env:CARGO_PROFILE_RELEASE_INCREMENTAL = 'true'
$env:CARGO_PROFILE_RELEASE_CODEGEN_UNITS = '256'

& cargo.exe build --locked --release --manifest-path (Join-Path $repositoryRoot 'rust_server/Cargo.toml') `
    -p px_console_runtime --bin px_console --bin px_console_admin `
    -p px_pg --bin px_db -p px_relay_server --bin px_relay -p px_backup --bin px_backup `
    -p px_server_tray --bin px_server_tray `
    --target-dir $targetDirectory
if ($LASTEXITCODE -ne 0) { throw 'Focused Windows Server build failed.' }

New-Item -ItemType Directory -Path $candidateRoot -Force | Out-Null
New-Item -ItemType Directory -Path $stagingRoot | Out-Null
try {
    $stagedPackage = Join-Path $stagingRoot 'package'
    $setupName = "PixelsServer_${SuiteVersion}_Setup.exe"
    & python.exe (Join-Path $repositoryRoot 'scripts/assemble_single_server_windows.py') `
        --bin (Join-Path $targetDirectory 'release') `
        --static (Join-Path $repositoryRoot 'web/px_console/dist') `
        --postgresql-client $PostgreSqlClientRoot `
        --output $stagedPackage `
        --suite-version $SuiteVersion
    if ($LASTEXITCODE -ne 0) { throw 'Windows Server candidate assembly failed.' }
    & python.exe (Join-Path $repositoryRoot 'setup/make_single_server.py') `
        --package $stagedPackage --output (Join-Path $stagingRoot $setupName)
    if ($LASTEXITCODE -ne 0) { throw 'Windows Server candidate Setup build failed.' }

    if (Test-Path -LiteralPath $outputRoot) { Move-Item -LiteralPath $outputRoot -Destination $previousRoot }
    try {
        Move-Item -LiteralPath $stagingRoot -Destination $outputRoot
    } catch {
        if (Test-Path -LiteralPath $previousRoot) {
            Move-Item -LiteralPath $previousRoot -Destination $outputRoot
        }
        throw
    }
    if (Test-Path -LiteralPath $previousRoot) {
        Remove-Item -LiteralPath $previousRoot -Recurse -Force
    }
    Write-Output "CANDIDATE $(Join-Path $outputRoot $setupName)"
} finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
}
