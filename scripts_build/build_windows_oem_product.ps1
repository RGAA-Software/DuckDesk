[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('cloud_node', 'client', 'remote')]
    [string]$Product,

    [string]$OemProfile = '',

    [switch]$PreflightOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$python = Get-Command python -ErrorAction Stop
$nodeRequired = $Product -in @('cloud_node', 'remote')
$productTarget = switch ($Product) {
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
if ([string]::IsNullOrWhiteSpace($OemProfile)) {
    $OemProfile = [Environment]::GetEnvironmentVariable('PIXELS_OEM_RELEASE_PROFILE')
}
if ([string]::IsNullOrWhiteSpace($OemProfile) -or -not (Test-Path -LiteralPath $OemProfile -PathType Leaf)) {
    throw 'Windows OEM release builds require PIXELS_OEM_RELEASE_PROFILE or -OemProfile.'
}
$resolvedOemProfile = [IO.Path]::GetFullPath($OemProfile)
$profileOutput = @(
    & $python.Source (Join-Path $repositoryRoot 'scripts\oem_release_profile.py') `
        --profile $resolvedOemProfile --product $Product --windows-json 2>&1
)
if ($LASTEXITCODE -ne 0) {
    throw "OEM Windows release profile validation failed: $($profileOutput -join [Environment]::NewLine)"
}
$oemConfiguration = ($profileOutput -join [Environment]::NewLine) | ConvertFrom-Json
$oemId = [string]$oemConfiguration.oem_id
$oemProductRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build_official\$Product\oem\$oemId"))
$expectedOemRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build_official\$Product\oem"))
if (-not $oemProductRoot.StartsWith($expectedOemRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to build outside the product OEM root: $oemProductRoot"
}

$hadOemProfileEnvironment = Test-Path Env:PIXELS_OEM_RELEASE_PROFILE
$previousOemProfileEnvironment = [Environment]::GetEnvironmentVariable('PIXELS_OEM_RELEASE_PROFILE')
$env:PIXELS_OEM_RELEASE_PROFILE = $resolvedOemProfile
try {
    $setupArguments = @(
        (Join-Path $repositoryRoot 'setup\make_setup.py'), '--product', $Product, '--distribution', 'oem',
        '--oem-profile', $resolvedOemProfile, '--preflight-only'
    )
    Invoke-NativeChecked -FilePath $python.Source -Arguments $setupArguments

    $prepareDistributionScript = Join-Path $repositoryRoot 'scripts\prepare_windows_distribution.py'
    Invoke-NativeChecked -FilePath $python.Source -Arguments @(
        $prepareDistributionScript, '--product', $Product, '--distribution', 'oem', '--validate-only'
    )
    if ($PreflightOnly) {
        Write-Host "Validated Windows $Product OEM release inputs for $oemId without cleaning outputs or assigning a version."
        exit 0
    }

    Invoke-NativeChecked -FilePath 'powershell.exe' -Arguments @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'clean_product_outputs.ps1'),
        '-Product', $Product, '-Distribution', 'oem', '-OemId', $oemId
    )
    if ($Product -eq 'cloud_node') {
        Invoke-NativeChecked -FilePath 'powershell.exe' -Arguments @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repositoryRoot 'third_party\cef\fetch_cef.ps1')
        )
    }
    Invoke-NativeChecked -FilePath $python.Source -Arguments @((Join-Path $repositoryRoot 'scripts\validate_product_manifests.py'))
    Invoke-NativeChecked -FilePath $python.Source -Arguments @((Join-Path $repositoryRoot 'scripts\validate_product_branding.py'))
    Invoke-NativeChecked -FilePath 'cmd.exe' -Arguments @('/d', '/c', (Join-Path $PSScriptRoot 'build_cpp_rdp_sdk.bat'))

    $versionOutput = @(
        & $python.Source (Join-Path $repositoryRoot 'set_product_version.py') '--product' $Product '--bump' '--json' 2>&1
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to assign the $Product OEM version: $($versionOutput -join [Environment]::NewLine)"
    }
    $assignedVersion = ($versionOutput -join [Environment]::NewLine) | ConvertFrom-Json
    if ($assignedVersion.company -ne 'Pixels') {
        throw "Windows source product owner must be Pixels; got '$($assignedVersion.company)'."
    }

    if ($nodeRequired) {
        Invoke-NativeChecked -FilePath 'node.exe' -Arguments @((Join-Path $repositoryRoot 'scripts\sync_web_protos.mjs'))
    }

    $updateDirectory = Join-Path $oemProductRoot 'update'
    Invoke-NativeChecked -FilePath $python.Source -Arguments @(
        $prepareDistributionScript, '--product', $Product, '--distribution', 'oem', '--output-dir', $updateDirectory
    )

    if ($Product -in @('cloud_node', 'remote')) {
        Invoke-NativeChecked -FilePath 'cmd.exe' -Arguments @(
            '/d', '/c', (Join-Path $PSScriptRoot 'build_cpp_rdp_policy.bat'), $Product, 'oem', $oemId
        )
    }
    if ($nodeRequired) {
        $webOutput = Join-Path $oemProductRoot 'web'
        $env:PIXELS_WEB_DISTRIBUTION = 'oem'
        $env:PIXELS_WEB_CLIENT_BUILD = [string]$assignedVersion.product_version_code
        $env:PIXELS_WEB_APPLICATION_NAME = [string]$oemConfiguration.application_name
        $env:PIXELS_WEB_ICON_FILE = [string]$oemConfiguration.web_icon_path
        $env:PIXELS_WEB_OEM_PROFILE_SHA256 = [string]$oemConfiguration.profile_sha256
        Push-Location (Join-Path $repositoryRoot 'web\px_web_client')
        try {
            Invoke-NativeChecked -FilePath 'npm.cmd' -Arguments @('ci')
            Invoke-NativeChecked -FilePath 'npm.cmd' -Arguments @(
                'run', 'build', '--', '--outDir', $webOutput, '--emptyOutDir'
            )
        } finally {
            Pop-Location
            Remove-Item Env:PIXELS_WEB_DISTRIBUTION, Env:PIXELS_WEB_CLIENT_BUILD, Env:PIXELS_WEB_APPLICATION_NAME,
                Env:PIXELS_WEB_ICON_FILE, Env:PIXELS_WEB_OEM_PROFILE_SHA256 -ErrorAction SilentlyContinue
        }
    }

    $updateRootCmakePath = (Join-Path $updateDirectory 'update-root.json').Replace('\', '/')
    $profileCmakePath = $resolvedOemProfile.Replace('\', '/')
    $env:CPP_PRODUCT = $Product
    $env:CPP_DISTRIBUTION = 'oem'
    $env:CPP_OEM_ID = $oemId
    $env:CPP_BUILD_DIR = "build_official\$Product\oem\$oemId\cmake"
    $env:CPP_BUILD_JOBS = '18'
    $env:CPP_CMAKE_DISTRIBUTION_ARGS = `
        "-DPX_UPDATE_ROOT_FILE:FILEPATH=`"$updateRootCmakePath`" -DPX_OEM_RELEASE_PROFILE:FILEPATH=`"$profileCmakePath`""
    try {
        Invoke-NativeChecked -FilePath 'cmd.exe' -Arguments @(
            '/d', '/c', (Join-Path $repositoryRoot 'scripts\build_cpp_target.bat'), $productTarget
        )
    } finally {
        Remove-Item Env:CPP_PRODUCT, Env:CPP_DISTRIBUTION, Env:CPP_OEM_ID, Env:CPP_BUILD_DIR, Env:CPP_BUILD_JOBS,
            Env:CPP_CMAKE_DISTRIBUTION_ARGS -ErrorAction SilentlyContinue
    }

    Invoke-NativeChecked -FilePath $python.Source -Arguments @(
        (Join-Path $repositoryRoot 'setup\make_setup.py'), '--product', $Product, '--distribution', 'oem',
        '--oem-profile', $resolvedOemProfile
    )
    $releaseDirectory = Join-Path $oemProductRoot "installer\$($assignedVersion.product_version)"
    Invoke-NativeChecked -FilePath $python.Source -Arguments @(
        (Join-Path $repositoryRoot 'scripts\verify_windows_installer_release.py'), 'single',
        '--release-dir', $releaseDirectory,
        '--expected-signer-sha256', ([string]$oemConfiguration.signer_certificate_sha256)
    )

    Write-Host "Completed $Product OEM $oemId release $($assignedVersion.product_version)."
} finally {
    if ($hadOemProfileEnvironment) {
        $env:PIXELS_OEM_RELEASE_PROFILE = $previousOemProfileEnvironment
    } else {
        Remove-Item Env:PIXELS_OEM_RELEASE_PROFILE -ErrorAction SilentlyContinue
    }
}
