[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('official', 'customer', 'oem')]
    [string]$Distribution,

    [Parameter(Mandatory = $true)]
    [ValidateSet('fast-release', 'release')]
    [string]$Configuration,

    [ValidateSet('', 'install')]
    [string]$Action = '',

    [switch]$PreflightOnly,

    [string]$AssignedVersionName = '',

    [int]$AssignedVersionCode = 0,

    [string]$AssignedCompany = '',

    [string]$OemProfile = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$androidRoot = Join-Path $repoRoot 'src\px_android'
$expectedAndroidRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build_official\android'))
$oemProfileTool = Join-Path $repoRoot 'scripts\oem_release_profile.py'
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw 'Python is required to validate Android identity and assign the product version.'
}
$oemConfiguration = $null
if ($Distribution -eq 'oem') {
    if ([string]::IsNullOrWhiteSpace($OemProfile)) {
        $OemProfile = [Environment]::GetEnvironmentVariable('PIXELS_OEM_RELEASE_PROFILE')
    }
    if ([string]::IsNullOrWhiteSpace($OemProfile) -or -not (Test-Path -LiteralPath $OemProfile -PathType Leaf)) {
        throw 'OEM Android builds require -OemProfile or PIXELS_OEM_RELEASE_PROFILE.'
    }
    $oemConfigurationOutput = @(& python $oemProfileTool --profile $OemProfile --android-json 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "OEM Android release profile validation failed: $($oemConfigurationOutput -join [Environment]::NewLine)"
    }
    $oemConfiguration = ($oemConfigurationOutput -join [Environment]::NewLine) | ConvertFrom-Json
    $androidProductRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build_official\android\oem\$($oemConfiguration.oem_id)"))
} else {
    if (-not [string]::IsNullOrWhiteSpace($OemProfile) -or
        -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('PIXELS_OEM_RELEASE_PROFILE'))) {
        throw 'Official and Customer Android builds must not configure an OEM release profile.'
    }
    $androidProductRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build_official\android\$Distribution"))
}
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
if ($Configuration -eq 'release' -and $Action) {
    throw 'The install action is only supported for fast Release device builds.'
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
    throw 'Android release builds require a version assigned by the approved Pixels matrix or OEM release orchestrator.'
}
if ($Action -eq 'install' -and -not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw 'adb is required for build_android_product.bat fast-release install.'
}
if (-not $androidProductRoot.StartsWith($expectedAndroidRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean outside the Android product root: $androidProductRoot"
}
$updateRootPath = [Environment]::GetEnvironmentVariable('PIXELS_UPDATE_ROOT_FILE')
if ([string]::IsNullOrWhiteSpace($updateRootPath) -or -not (Test-Path -LiteralPath $updateRootPath -PathType Leaf)) {
    throw 'PIXELS_UPDATE_ROOT_FILE must identify the approved signed TUF 1.0 initial root.'
}
if ($Distribution -in @('official', 'customer')) {
    if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('PIXELS_OFFICIAL_CONSOLE_URL'))) {
        throw 'Official and Customer builds require PIXELS_OFFICIAL_CONSOLE_URL.'
    }
} elseif (-not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('PIXELS_OFFICIAL_CONSOLE_URL'))) {
    throw 'OEM builds must not configure the Pixels Official Console URL.'
}
if ($Distribution -eq 'oem') {
    $actualUpdateRootSha256 = (Get-FileHash -LiteralPath $updateRootPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualUpdateRootSha256 -ne [string]$oemConfiguration.update_root_sha256) {
        throw 'The TUF initial root does not match the OEM release profile.'
    }
    $configuredCertificateSha256 = [Environment]::GetEnvironmentVariable('PIXELS_SIGNING_CERT_SHA256')
    $keystorePropertiesPath = Join-Path $androidRoot 'keystore.properties'
    if ([string]::IsNullOrWhiteSpace($configuredCertificateSha256) -and
        (Test-Path -LiteralPath $keystorePropertiesPath -PathType Leaf)) {
        $certificateProperty = Get-Content -LiteralPath $keystorePropertiesPath |
            Where-Object { $_ -match '^\s*certificateSha256\s*=' } |
            Select-Object -First 1
        if ($certificateProperty) {
            $configuredCertificateSha256 = ($certificateProperty -replace '^\s*certificateSha256\s*=\s*', '').Trim()
        }
    }
    $configuredCertificateSha256 = ([string]$configuredCertificateSha256 -replace '[:\s]', '').ToLowerInvariant()
    if ($configuredCertificateSha256 -ne [string]$oemConfiguration.signer_certificate_sha256) {
        throw 'The configured Android signing certificate does not match the OEM release profile.'
    }
    $env:PIXELS_OEM_ID = [string]$oemConfiguration.oem_id
    $env:PIXELS_RELEASE_NAMESPACE = [string]$oemConfiguration.release_namespace
    $env:PIXELS_OEM_PROFILE_SHA256 = [string]$oemConfiguration.profile_sha256
    $env:PIXELS_ANDROID_APPLICATION_ID = [string]$oemConfiguration.application_id
    $env:PIXELS_ANDROID_APPLICATION_NAME = [string]$oemConfiguration.application_name
    $env:PIXELS_ANDROID_BRAND_COMPANY = [string]$oemConfiguration.company_name
    $env:PIXELS_ANDROID_ICON_FOREGROUND_FILE = [string]$oemConfiguration.icon_foreground_path
    $env:PIXELS_ANDROID_ICON_BACKGROUND_FILE = [string]$oemConfiguration.icon_background_path
} else {
    Remove-Item Env:PIXELS_OEM_ID, Env:PIXELS_RELEASE_NAMESPACE, Env:PIXELS_OEM_PROFILE_SHA256,
        Env:PIXELS_ANDROID_APPLICATION_ID, Env:PIXELS_ANDROID_APPLICATION_NAME, Env:PIXELS_ANDROID_BRAND_COMPANY,
        Env:PIXELS_ANDROID_ICON_FOREGROUND_FILE, Env:PIXELS_ANDROID_ICON_BACKGROUND_FILE -ErrorAction SilentlyContinue
}
$env:PIXELS_DISTRIBUTION = $Distribution
$env:PIXELS_VALIDATE_DISTRIBUTION = '1'
$env:PIXELS_VALIDATE_RELEASE = '1'
Push-Location $androidRoot
try {
    $validationTask = ':app:validateReleaseConfiguration'
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

if ($Distribution -eq 'oem') {
    $oemResourceRoot = Join-Path $androidBuildRoot 'generated\oem-branding\res'
    $oemDrawableRoot = Join-Path $oemResourceRoot 'drawable-nodpi'
    $oemMipmapRoot = Join-Path $oemResourceRoot 'mipmap-anydpi-v26'
    $oemValuesRoot = Join-Path $oemResourceRoot 'values'
    [IO.Directory]::CreateDirectory($oemDrawableRoot) | Out-Null
    [IO.Directory]::CreateDirectory($oemMipmapRoot) | Out-Null
    [IO.Directory]::CreateDirectory($oemValuesRoot) | Out-Null
    Copy-Item -LiteralPath ([string]$oemConfiguration.icon_foreground_path) -Destination (Join-Path $oemDrawableRoot 'oem_icon_foreground.png')
    Copy-Item -LiteralPath ([string]$oemConfiguration.icon_background_path) -Destination (Join-Path $oemDrawableRoot 'oem_icon_background.png')
    $adaptiveIconXml = @'
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@drawable/oem_icon_background" />
    <foreground android:drawable="@drawable/oem_icon_foreground" />
</adaptive-icon>
'@
    Set-Content -LiteralPath (Join-Path $oemMipmapRoot 'ic_oem_launcher.xml') -Value $adaptiveIconXml -Encoding utf8
    Set-Content -LiteralPath (Join-Path $oemMipmapRoot 'ic_oem_launcher_round.xml') -Value $adaptiveIconXml -Encoding utf8
    $oemThemeXml = @'
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <style name="Theme.Oem.Starting" parent="Theme.SplashScreen">
        <item name="windowSplashScreenBackground">@drawable/oem_icon_background</item>
        <item name="windowSplashScreenAnimatedIcon">@drawable/oem_icon_foreground</item>
        <item name="postSplashScreenTheme">@style/Theme.Pixels</item>
    </style>
</resources>
'@
    Set-Content -LiteralPath (Join-Path $oemValuesRoot 'oem_theme.xml') -Value $oemThemeXml -Encoding utf8
    $env:PIXELS_ANDROID_BRAND_RESOURCE_ROOT = $oemResourceRoot
} else {
    Remove-Item Env:PIXELS_ANDROID_BRAND_RESOURCE_ROOT -ErrorAction SilentlyContinue
}

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
$assignedAndroidVersion = ($versionOutput -join [Environment]::NewLine) | ConvertFrom-Json
if ($hasAssignedVersion -and
    ([string]$assignedAndroidVersion.product_version -ne $AssignedVersionName -or
        [int]$assignedAndroidVersion.product_version_code -ne $AssignedVersionCode -or
        [string]$assignedAndroidVersion.company -ne $AssignedCompany)) {
    throw 'The assigned Android release version no longer matches the product manifest.'
}
$env:PIXELS_VERSION_NAME = [string]$assignedAndroidVersion.product_version
$env:PIXELS_VERSION_CODE = [string]$assignedAndroidVersion.product_version_code
$env:PIXELS_COMPANY = [string]$assignedAndroidVersion.company
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
    Remove-Item Env:PIXELS_FAST_RELEASE -ErrorAction SilentlyContinue
    & (Join-Path $androidRoot 'scripts\build_release.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "Android release build failed with exit code $LASTEXITCODE. The assigned version remains consumed."
    }
    exit 0
}

$env:PIXELS_FAST_RELEASE = '1'
$tasks = @('--project-cache-dir', (Join-Path $androidBuildRoot 'project-cache'), ':app:lintRelease', 'testDebugUnitTest', ':app:assembleRelease', '--stacktrace')
Push-Location $androidRoot
try {
    & $gradle @tasks
    if ($LASTEXITCODE -ne 0) {
        throw "Android fast Release build failed with exit code $LASTEXITCODE. The assigned version remains consumed."
    }
} finally {
    Pop-Location
    Remove-Item Env:PIXELS_FAST_RELEASE -ErrorAction SilentlyContinue
}

$metadataPath = Join-Path $androidBuildRoot 'app\outputs\apk\release\output-metadata.json'
if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
    throw 'Gradle completed without producing fast Release APK metadata.'
}
$apkMetadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$apkMetadataElement = @($apkMetadata.elements)[0]
$expectedVersionName = [string]$env:PIXELS_VERSION_NAME
if ([string]$apkMetadataElement.versionName -ne $expectedVersionName -or
    [int]$apkMetadataElement.versionCode -ne [int]$env:PIXELS_VERSION_CODE) {
    throw "Fast Release APK metadata does not match $expectedVersionName ($($env:PIXELS_VERSION_CODE))."
}
$apkPath = Join-Path (Split-Path -Parent $metadataPath) ([string]$apkMetadataElement.outputFile)
if (-not (Test-Path -LiteralPath $apkPath -PathType Leaf)) {
    throw "Fast Release APK is missing: $apkPath"
}

$distRoot = Join-Path $androidProductRoot 'dist'
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$artifactBrand = if ($Distribution -eq 'oem') { "OEM-$($oemConfiguration.oem_id)" } else { "Pixels-$Distribution" }
$destination = Join-Path $distRoot "$artifactBrand-$($env:PIXELS_VERSION_NAME)-fast-release-arm64-v8a.apk"
$temporaryDestination = "$destination.tmp"
Copy-Item -LiteralPath $apkPath -Destination $temporaryDestination -Force
Move-Item -LiteralPath $temporaryDestination -Destination $destination -Force
$sourceHash = Get-Sha256File -Path $apkPath
$destinationHash = Get-Sha256File -Path $destination
if ($sourceHash -ne $destinationHash) {
    throw 'Published Android fast Release APK hash does not match the Gradle artifact.'
}
$retiredMediaAudit = Join-Path $repoRoot 'scripts\audit_android_retired_media.py'
& python $retiredMediaAudit $destination
if ($LASTEXITCODE -ne 0) {
    throw 'Published Android fast Release APK contains a retired central media artifact or could not be audited.'
}

if ($Action -eq 'install') {
    & adb install -r $destination
    if ($LASTEXITCODE -ne 0) {
        throw "adb install -r failed with exit code $LASTEXITCODE."
    }
}

Write-Host "Android fast Release artifact: $destination"
Write-Host "SHA-256: $destinationHash"
