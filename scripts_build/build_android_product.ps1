[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('debug', 'release')]
    [string]$Configuration,

    [ValidateSet('', 'install')]
    [string]$Action = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$androidRoot = Join-Path $repoRoot 'src\px_android'
$androidBuildRoot = Join-Path $repoRoot 'build_official\android\gradle'
$androidNativeRoot = Join-Path $repoRoot 'build_official\android\native'
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
if ($Action -eq 'install' -and -not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw 'adb is required for build_android_product.bat debug install.'
}

& (Join-Path $PSScriptRoot 'clean_product_outputs.ps1') -Product android

$versionOutput = @(& python $versionTool --product android --bump --json 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to assign the Android product version: $($versionOutput -join [Environment]::NewLine)"
}
$version = ($versionOutput -join [Environment]::NewLine) | ConvertFrom-Json
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

Write-Host "Building Pixels Android $($env:PIXELS_VERSION_NAME) ($($env:PIXELS_VERSION_CODE)) $Configuration."

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

$distRoot = Join-Path $repoRoot 'build_official\android\dist'
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$destination = Join-Path $distRoot "Pixels-$($env:PIXELS_VERSION_NAME)-debug-arm64-v8a.apk"
$temporaryDestination = "$destination.tmp"
Copy-Item -LiteralPath $apkPath -Destination $temporaryDestination -Force
Move-Item -LiteralPath $temporaryDestination -Destination $destination -Force
$sourceHash = Get-Sha256File -Path $apkPath
$destinationHash = Get-Sha256File -Path $destination
if ($sourceHash -ne $destinationHash) {
    throw 'Published Android debug APK hash does not match the Gradle artifact.'
}

if ($Action -eq 'install') {
    & adb install -r $destination
    if ($LASTEXITCODE -ne 0) {
        throw "adb install -r failed with exit code $LASTEXITCODE."
    }
}

Write-Host "Android debug artifact: $destination"
Write-Host "SHA-256: $destinationHash"
