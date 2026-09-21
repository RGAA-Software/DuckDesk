[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('cloud_node', 'client', 'remote')]
    [string]$Product
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $repoRoot "build_official\$Product"
$python = Get-Command python -ErrorAction Stop
$nodeRequired = $Product -in @('cloud_node', 'remote')
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
Invoke-NativeChecked -FilePath $python.Source -Arguments @(
    $prepareScript, '--product', $Product, '--distribution', 'customer', '--matrix-customer', '--validate-only'
)

Invoke-NativeChecked -FilePath 'powershell.exe' -Arguments @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'clean_product_outputs.ps1'), '-Product', $Product
)
if ($Product -eq 'cloud_node') {
    Invoke-NativeChecked -FilePath 'powershell.exe' -Arguments @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repoRoot 'third_party\cef\fetch_cef.ps1')
    )
}
Invoke-NativeChecked -FilePath $python.Source -Arguments @((Join-Path $repoRoot 'scripts\validate_product_manifests.py'))
Invoke-NativeChecked -FilePath $python.Source -Arguments @((Join-Path $repoRoot 'scripts\validate_product_branding.py'))
Invoke-NativeChecked -FilePath 'cmd.exe' -Arguments @('/d', '/c', (Join-Path $PSScriptRoot 'build_cpp_rdp_sdk.bat'))

$versionOutput = & $python.Source (Join-Path $repoRoot 'set_product_version.py') '--product' $Product '--bump' '--json'
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

foreach ($distribution in @('official', 'customer')) {
    $distributionRoot = Join-Path $buildRoot $distribution
    $deploymentDirectory = Join-Path $distributionRoot 'deployment'
    $prepareArguments = @(
        $prepareScript, '--product', $Product, '--distribution', $distribution, '--output-dir', $deploymentDirectory
    )
    if ($distribution -eq 'customer') {
        $prepareArguments += '--matrix-customer'
    }
    Invoke-NativeChecked -FilePath $python.Source -Arguments $prepareArguments

    if ($Product -in @('cloud_node', 'remote')) {
        Invoke-NativeChecked -FilePath 'cmd.exe' -Arguments @(
            '/d', '/c', (Join-Path $PSScriptRoot 'build_cpp_rdp_policy.bat'), $Product, $distribution
        )
    }
    if ($nodeRequired) {
        $webOutput = Join-Path $distributionRoot 'web'
        $env:PIXELS_WEB_DISTRIBUTION = $distribution
        $env:PIXELS_WEB_DEPLOYMENT_POLICY_FILE = Join-Path $deploymentDirectory 'deployment-policy.json'
        $env:PIXELS_WEB_DEPLOYMENT_TRUST_FILE = Join-Path $deploymentDirectory 'deployment-trust.json'
        $env:PIXELS_WEB_CLIENT_BUILD = [string]$version.product_version_code
        Push-Location (Join-Path $repoRoot 'web\px_web_client')
        try {
            Invoke-NativeChecked -FilePath 'npm.cmd' -Arguments @('ci')
            Invoke-NativeChecked -FilePath 'npm.cmd' -Arguments @('run', 'build', '--', '--outDir', $webOutput, '--emptyOutDir')
        } finally {
            Pop-Location
            Remove-Item Env:PIXELS_WEB_DISTRIBUTION, Env:PIXELS_WEB_DEPLOYMENT_POLICY_FILE, Env:PIXELS_WEB_DEPLOYMENT_TRUST_FILE, `
                Env:PIXELS_WEB_CLIENT_BUILD -ErrorAction SilentlyContinue
        }
    }

    $env:CPP_PRODUCT = $Product
    $env:CPP_DISTRIBUTION = $distribution
    $env:CPP_BUILD_DIR = "build_official\$Product\$distribution\cmake"
    $env:CPP_BUILD_JOBS = '18'
    $env:CPP_CMAKE_DISTRIBUTION_ARGS = "-DPX_DEPLOYMENT_POLICY_DIR=build_official/$Product/$distribution/deployment"
    try {
        Invoke-NativeChecked -FilePath 'cmd.exe' -Arguments @(
            '/d', '/c', (Join-Path $repoRoot 'scripts\build_cpp_target.bat'), $target
        )
    } finally {
        Remove-Item Env:CPP_PRODUCT, Env:CPP_DISTRIBUTION, Env:CPP_BUILD_DIR, Env:CPP_BUILD_JOBS, Env:CPP_CMAKE_DISTRIBUTION_ARGS `
            -ErrorAction SilentlyContinue
    }
    Invoke-NativeChecked -FilePath $python.Source -Arguments @(
        (Join-Path $repoRoot 'setup\make_setup.py'), '--product', $Product, '--distribution', $distribution
    )
}

Write-Host "Completed Pixels $Product $($version.product_version) official+customer release matrix."
