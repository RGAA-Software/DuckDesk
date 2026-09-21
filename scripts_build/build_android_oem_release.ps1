[CmdletBinding()]
param(
    [string]$OemProfile = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$productBuilder = Join-Path $PSScriptRoot 'build_android_product.ps1'
$versionTool = Join-Path $repositoryRoot 'set_product_version.py'
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw 'Python is required to assign the Android OEM product version.'
}
if ([string]::IsNullOrWhiteSpace($OemProfile)) {
    $OemProfile = [Environment]::GetEnvironmentVariable('PIXELS_OEM_RELEASE_PROFILE')
}
if ([string]::IsNullOrWhiteSpace($OemProfile) -or -not (Test-Path -LiteralPath $OemProfile -PathType Leaf)) {
    throw 'OEM Android release builds require PIXELS_OEM_RELEASE_PROFILE or -OemProfile.'
}

function Invoke-OemProductBuilder {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$BuilderArguments
    )

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $productBuilder `
        -Distribution oem -Configuration release -OemProfile $OemProfile @BuilderArguments
    if ($LASTEXITCODE -ne 0) {
        throw "OEM Android product builder failed with exit code $LASTEXITCODE."
    }
}

Invoke-OemProductBuilder -BuilderArguments @('-PreflightOnly')

$versionOutput = @(& python $versionTool --product android --bump --json 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to assign the Android OEM release version: $($versionOutput -join [Environment]::NewLine)"
}
$assignedAndroidVersion = ($versionOutput -join [Environment]::NewLine) | ConvertFrom-Json
if ($assignedAndroidVersion.company -ne 'Pixels') {
    throw "Android product owner must be Pixels; got '$($assignedAndroidVersion.company)'."
}

Invoke-OemProductBuilder -BuilderArguments @(
    '-AssignedVersionName', [string]$assignedAndroidVersion.product_version,
    '-AssignedVersionCode', [string]$assignedAndroidVersion.product_version_code,
    '-AssignedCompany', [string]$assignedAndroidVersion.company
)
