[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('cloud_node', 'client', 'remote')]
    [string]$Product,

    [switch]$Incremental,

    [ValidateRange(1, 10)]
    [int]$Jobs = 10,

    [ValidatePattern('^[a-z0-9][a-z0-9-]{0,63}$')]
    [string]$CandidateId
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
& (Join-Path $PSScriptRoot '../scripts/limit_build_cpu.ps1')

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $repoRoot "build_official\$Product"
if ($CandidateId -and -not $Incremental) {
    throw 'CandidateId requires Incremental so existing release installers and candidates are preserved.'
}
if ($CandidateId -and (Test-Path -LiteralPath (Join-Path $buildRoot "official/candidates/$CandidateId"))) {
    throw "Candidate output already exists and will not be overwritten: $CandidateId"
}
$python = Get-Command python -ErrorAction Stop
$nodeRequired = $Product -in @('cloud_node', 'remote')
$officialConsoleUrl = [Environment]::GetEnvironmentVariable('PIXELS_OFFICIAL_CONSOLE_URL')
if (-not [string]::IsNullOrWhiteSpace($officialConsoleUrl)) {
    throw 'All Pixels Windows products configure Console after installation; clear PIXELS_OFFICIAL_CONSOLE_URL before packaging.'
}
$officialConsoleUrl = ''
$env:CARGO_BUILD_JOBS = [string]$Jobs
$env:VCPKG_MAX_CONCURRENCY = [string]$Jobs
$env:CPP_BUILD_JOBS = [string]$Jobs
$target = switch ($Product) {
    'cloud_node' { 'px_build_cloud_node_all' }
    'client' { 'px_build_client_all' }
    'remote' { 'px_build_remote_all' }
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

if ($nodeRequired) {
    Get-Command node -ErrorAction Stop | Out-Null
    Get-Command npm -ErrorAction Stop | Out-Null
}

Invoke-NativeChecked -FilePath $python.Source -Arguments @(
    (Join-Path $repoRoot 'setup\make_setup.py'), '--product', $Product, '--distribution', 'official', '--preflight-only'
)

$prepareScript = Join-Path $repoRoot 'scripts\prepare_windows_distribution.py'
Invoke-NativeChecked -FilePath $python.Source -Arguments @(
    $prepareScript, '--product', $Product, '--distribution', 'official', '--validate-only'
)
if ($Incremental) {
    $existingBuildCache = Join-Path $buildRoot 'official\cmake\CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $existingBuildCache -PathType Leaf)) {
        throw "Incremental packaging requires an existing optimized $Product/official build tree."
    }
    Write-Host "Reusing optimized $Product/official build tree for incremental packaging."
} else {
    Invoke-NativeChecked -FilePath 'powershell.exe' -Arguments @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'clean_product_outputs.ps1'), '-Product', $Product,
        '-Distribution', 'official'
    )
}
if ($Product -eq 'cloud_node') {
    Invoke-NativeChecked -FilePath 'powershell.exe' -Arguments @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repoRoot 'third_party\cef\fetch_cef.ps1')
    )
}
Invoke-NativeChecked -FilePath $python.Source -Arguments @((Join-Path $repoRoot 'scripts\validate_product_manifests.py'))
Invoke-NativeChecked -FilePath $python.Source -Arguments @((Join-Path $repoRoot 'scripts\validate_product_branding.py'))
Invoke-NativeChecked -FilePath 'powershell.exe' -Arguments @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repoRoot 'scripts/prepare_rdp_sdk.ps1'), '-Jobs', [string]$Jobs
)
Invoke-NativeChecked -FilePath 'powershell.exe' -Arguments @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'build_transport.ps1'), '-Profile', 'Publication',
    '-Jobs', [string]$Jobs
)

$versionAction = if ($CandidateId) { '--show' } else { '--bump' }
$versionOutput = & $python.Source (Join-Path $repoRoot 'set_product_version.py') '--product' $Product $versionAction '--json'
if ($LASTEXITCODE -ne 0) {
    throw "Unable to assign the $Product version; the distribution build did not start."
}
$version = $versionOutput | ConvertFrom-Json
if ($version.company -ne 'Pixels') {
    throw "Windows product company must be Pixels; got '$($version.company)'."
}

if ($nodeRequired) {
    Invoke-NativeChecked -FilePath 'node.exe' -Arguments @((Join-Path $repoRoot 'scripts\sync_web_protos.mjs'))
}

foreach ($distribution in @('official')) {
    $distributionRoot = Join-Path $buildRoot $distribution
    $updateDirectory = Join-Path $distributionRoot 'update'
    $prepareArguments = @(
        $prepareScript, '--product', $Product, '--distribution', $distribution, '--output-dir', $updateDirectory
    )
    Invoke-NativeChecked -FilePath $python.Source -Arguments $prepareArguments

    if ($Product -in @('cloud_node', 'remote')) {
        Invoke-NativeChecked -FilePath 'cmd.exe' -Arguments @(
            '/d', '/c', (Join-Path $PSScriptRoot 'build_cpp_rdp_policy.bat'), $Product, $distribution
        )
    }
    if ($nodeRequired) {
        $webOutput = Join-Path $distributionRoot 'web'
        $env:PIXELS_WEB_DISTRIBUTION = $distribution
        $env:PIXELS_WEB_CLIENT_BUILD = [string]$version.product_version_code
        Push-Location (Join-Path $repoRoot 'web\px_web_client')
        try {
            Invoke-NativeChecked -FilePath 'npm.cmd' -Arguments @('ci')
            Invoke-NativeChecked -FilePath 'npm.cmd' -Arguments @('run', 'build', '--', '--outDir', $webOutput, '--emptyOutDir')
        } finally {
            Pop-Location
            Remove-Item Env:PIXELS_WEB_DISTRIBUTION, Env:PIXELS_WEB_CLIENT_BUILD -ErrorAction SilentlyContinue
        }
    }

    $env:CPP_PRODUCT = $Product
    $env:CPP_DISTRIBUTION = $distribution
    $env:CPP_BUILD_DIR = "build_official\$Product\$distribution\cmake"
    $env:CPP_BUILD_JOBS = [string]$Jobs
    $env:CPP_CMAKE_DISTRIBUTION_ARGS =
        "-DPX_UPDATE_ROOT_FILE=build_official/$Product/$distribution/update/update-root.json " +
        "-DPX_OFFICIAL_CONSOLE_ORIGIN:STRING=`"$officialConsoleUrl`""
    try {
        Invoke-NativeChecked -FilePath 'cmd.exe' -Arguments @(
            '/d', '/c', (Join-Path $repoRoot 'scripts\build_cpp_target.bat'), $target
        )
    } finally {
        Remove-Item Env:CPP_PRODUCT, Env:CPP_DISTRIBUTION, Env:CPP_BUILD_DIR, Env:CPP_BUILD_JOBS, Env:CPP_CMAKE_DISTRIBUTION_ARGS `
            -ErrorAction SilentlyContinue
    }
    $setupArguments = @(
        (Join-Path $repoRoot 'setup\make_setup.py'), '--product', $Product, '--distribution', $distribution
    )
    if ($CandidateId) { $setupArguments += @('--candidate-id', $CandidateId) }
    Invoke-NativeChecked -FilePath $python.Source -Arguments $setupArguments
}

Write-Host "Completed Pixels $Product $($version.product_version) single-package release."
