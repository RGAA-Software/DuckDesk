[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('official', 'customer')]
    [string]$Distribution,

    [Parameter(Mandatory = $true)]
    [ValidateSet('debug', 'release')]
    [string]$Configuration,

    [ValidateSet('', 'install')]
    [string]$Action = '',

    [switch]$PreflightOnly,

    [string]$AssignedVersionName = '',

    [int]$AssignedVersionCode = 0,

    [string]$AssignedCompany = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$androidRoot = Join-Path $repoRoot 'src\px_android'
$androidProductRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build_official\android\$Distribution"))
$expectedAndroidRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build_official\android'))
$androidBuildRoot = Join-Path $androidProductRoot 'gradle'
$androidNativeRoot = Join-Path $androidProductRoot 'native'
$gradle = Join-Path $androidRoot 'gradlew.bat'
$versionTool = Join-Path $repoRoot 'set_product_version.py'

function Get-Sha256File {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $algorithm = [System.Security.Cryptography.SHA256]::Create()
        try {
            return [System.BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '')
        } finally {
            $algorithm.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $gradle -PathType Leaf)) {
    throw "Android Gradle wrapper is missing: $gradle"
}
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw 'Python is required to assign the Android product version.'
}
if ($Configuration -eq 'release' -and $Action) {
    throw 'The install action is only supported for debug builds.'
}
if ($PreflightOnly -and $Action) {
    throw 'Preflight-only validation cannot install an Android package.'
}
$hasAssignedVersion = -not [string]::IsNullOrWhiteSpace($AssignedVersionName) -or $AssignedVersionCode -ne 0 -or
    -not [string]::IsNullOrWhiteSpace($AssignedCompany)
if ($hasAssignedVersion -and
    ([string]::IsNullOrWhiteSpace($AssignedVersionName) -or $AssignedVersionCode -le 0 -or [string]::IsNullOrWhiteSpace($AssignedCompany))) {
    throw 'Assigned Android version name, code and company must be supplied together.'
}
if ($PreflightOnly -and $hasAssignedVersion) {
    throw 'Preflight-only validation does not accept an assigned Android version.'
}
if ($Configuration -eq 'release' -and -not $PreflightOnly -and -not $hasAssignedVersion) {
    throw 'Android release builds require the matrix-assigned version; use build_android_product.bat release.'
}
if ($Action -eq 'install' -and -not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw 'adb is required for build_android_product.bat debug install.'
}
if (-not $androidProductRoot.StartsWith($expectedAndroidRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean outside the Android product root: $androidProductRoot"
}
$trustStorePath = [Environment]::GetEnvironmentVariable('PIXELS_DEPLOYMENT_TRUST_STORE_FILE')
if ([string]::IsNullOrWhiteSpace($trustStorePath) -or -not (Test-Path -LiteralPath $trustStorePath -PathType Leaf)) {
    throw 'PIXELS_DEPLOYMENT_TRUST_STORE_FILE must identify the approved canonical public trust store.'
}
if ($Distribution -eq 'official') {
    if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('PIXELS_EXPECTED_DEPLOYMENT_ID')) -or
        [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('PIXELS_OFFICIAL_CONSOLE_URL'))) {
        throw 'Official builds require PIXELS_EXPECTED_DEPLOYMENT_ID and PIXELS_OFFICIAL_CONSOLE_URL.'
    }
} elseif (-not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('PIXELS_EXPECTED_DEPLOYMENT_ID')) -or
    -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('PIXELS_OFFICIAL_CONSOLE_URL'))) {
    throw 'Customer builds must not configure PIXELS_EXPECTED_DEPLOYMENT_ID or PIXELS_OFFICIAL_CONSOLE_URL.'
}
$env:PIXELS_DISTRIBUTION = $Distribution
$env:PIXELS_VALIDATE_DISTRIBUTION = '1'
if ($Configuration -eq 'release') {
    $env:PIXELS_VALIDATE_RELEASE = '1'
}
Push-Location $androidRoot
try {
    $validationTask = if ($Configuration -eq 'release') { ':app:validateReleaseConfiguration' } else { ':app:validateDistributionConfiguration' }
    & $gradle $validationTask '--quiet'
    if ($LASTEXITCODE -ne 0) {
        throw 'Android distribution identity preflight failed; existing artifacts and version were not changed.'
    }
} finally {
    Pop-Location
    Remove-Item Env:PIXELS_VALIDATE_DISTRIBUTION -ErrorAction SilentlyContinue
    Remove-Item Env:PIXELS_VALIDATE_RELEASE -ErrorAction SilentlyContinue
}
if ($PreflightOnly) {
    Write-Host "Validated Android $Distribution distribution inputs without changing artifacts or version."
    exit 0
}

if ([IO.Directory]::Exists($androidProductRoot)) {
    Write-Host "Removing generated Android $Distribution output: $androidProductRoot"
    [IO.Directory]::Delete($androidProductRoot, $true)
} elseif ([IO.File]::Exists($androidProductRoot)) {
    throw "Expected an Android product directory but found a file: $androidProductRoot"
}
[IO.Directory]::CreateDirectory($androidProductRoot) | Out-Null

$versionArguments = @('--product', 'android', '--json')
if ($hasAssignedVersion) {
    $versionArguments += '--show'
} else {
    $versionArguments += '--bump'
}
$versionOutput = @(& python $versionTool @versionArguments 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to assign the Android product version: $($versionOutput -join [Environment]::NewLine)"
}
$version = ($versionOutput -join [Environment]::NewLine) | ConvertFrom-Json
if ($hasAssignedVersion -and
    ([string]$version.product_version -ne $AssignedVersionName -or [int]$version.product_version_code -ne $AssignedVersionCode -or
        [string]$version.company -ne $AssignedCompany)) {
    throw 'The assigned Android release version no longer matches the product manifest.'
}
$env:PIXELS_VERSION_NAME = [string]$version.product_version
$env:PIXELS_VERSION_CODE = [string]$version.product_version_code
$env:PIXELS_COMPANY = [string]$version.company
if ($env:PIXELS_COMPANY -ne 'Pixels') {
    throw "Android product company must be Pixels; got '$($env:PIXELS_COMPANY)'."
}
$revision = (& git -C $repoRoot rev-parse --short=12 HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
    throw 'Unable to resolve the Git revision after assigning the Android product version.'
}
$env:PIXELS_GIT_REVISION = $revision
$env:PIXELS_ANDROID_BUILD_ROOT = $androidBuildRoot
$env:PIXELS_ANDROID_NATIVE_ROOT = $androidNativeRoot

Write-Host "Building Pixels Android $Distribution $($env:PIXELS_VERSION_NAME) ($($env:PIXELS_VERSION_CODE)) $Configuration."

if ($Configuration -eq 'release') {
    & (Join-Path $androidRoot 'scripts\build_release.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "Android release build failed with exit code $LASTEXITCODE. The assigned version remains consumed."
    }
    exit 0
}

$tasks = @('--project-cache-dir', (Join-Path $androidBuildRoot 'project-cache'), ':app:lintDebug', 'testDebugUnitTest', ':app:assembleDebug', '--stacktrace')
Push-Location $androidRoot
try {
    & $gradle @tasks
    if ($LASTEXITCODE -ne 0) {
        throw "Android debug build failed with exit code $LASTEXITCODE. The assigned version remains consumed."
    }
} finally {
    Pop-Location
}

$metadataPath = Join-Path $androidBuildRoot 'app\outputs\apk\debug\output-metadata.json'
if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
    throw 'Gradle completed without producing debug APK metadata.'
}
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$element = @($metadata.elements)[0]
$expectedVersionName = "$($env:PIXELS_VERSION_NAME)-debug"
if ([string]$element.versionName -ne $expectedVersionName -or [int]$element.versionCode -ne [int]$env:PIXELS_VERSION_CODE) {
    throw "Debug APK metadata does not match $expectedVersionName ($($env:PIXELS_VERSION_CODE))."
}
$apkPath = Join-Path (Split-Path -Parent $metadataPath) ([string]$element.outputFile)
if (-not (Test-Path -LiteralPath $apkPath -PathType Leaf)) {
    throw "Debug APK is missing: $apkPath"
}

$distRoot = Join-Path $androidProductRoot 'dist'
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$destination = Join-Path $distRoot "Pixels-$Distribution-$($env:PIXELS_VERSION_NAME)-debug-arm64-v8a.apk"
$temporaryDestination = "$destination.tmp"
Copy-Item -LiteralPath $apkPath -Destination $temporaryDestination -Force
Move-Item -LiteralPath $temporaryDestination -Destination $destination -Force
$sourceHash = Get-Sha256File -Path $apkPath
$destinationHash = Get-Sha256File -Path $destination
if ($sourceHash -ne $destinationHash) {
    throw 'Published Android debug APK hash does not match the Gradle artifact.'
}
$retiredMediaAudit = Join-Path $repoRoot 'scripts\audit_android_retired_media.py'
& python $retiredMediaAudit $destination
if ($LASTEXITCODE -ne 0) {
    throw 'Published Android debug APK contains a retired central media artifact or could not be audited.'
}

if ($Action -eq 'install') {
    & adb install -r $destination
    if ($LASTEXITCODE -ne 0) {
        throw "adb install -r failed with exit code $LASTEXITCODE."
    }
}

Write-Host "Android debug artifact: $destination"
Write-Host "SHA-256: $destinationHash"
