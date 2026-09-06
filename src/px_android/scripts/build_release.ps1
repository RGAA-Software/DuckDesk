[CmdletBinding()]
param(
    [switch]$SkipClean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$androidRoot = Split-Path -Parent $PSScriptRoot
$gradle = Join-Path $androidRoot 'gradlew.bat'
$metadataPath = Join-Path $androidRoot 'app\build\outputs\apk\release\output-metadata.json'
$bundlePath = Join-Path $androidRoot 'app\build\outputs\bundle\release\app-release.aab'
$mappingPath = Join-Path $androidRoot 'app\build\outputs\mapping\release\mapping.txt'
$propertiesPath = Join-Path $androidRoot 'keystore.properties'
$noticeSourceRoot = Join-Path $androidRoot 'feature-settings\src\main\res\raw'
$ffmpegSourceArchive = [Environment]::GetEnvironmentVariable('PIXELS_FFMPEG_SOURCE_ARCHIVE')
$lgplRelinkArchive = [Environment]::GetEnvironmentVariable('PIXELS_LGPL_RELINK_ARCHIVE')

$environmentSigningNames = @(
    'PIXELS_KEYSTORE_FILE',
    'PIXELS_KEYSTORE_PASSWORD',
    'PIXELS_KEY_ALIAS',
    'PIXELS_KEY_PASSWORD'
)
$missingEnvironmentSigning = @($environmentSigningNames | Where-Object {
    [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($_))
})
if ($missingEnvironmentSigning.Count -ne 0 -and -not (Test-Path -LiteralPath $propertiesPath -PathType Leaf)) {
    throw 'Configure all PIXELS_* signing variables or copy keystore.properties.example to the ignored keystore.properties file.'
}

if ([string]::IsNullOrWhiteSpace($ffmpegSourceArchive) -or
    -not (Test-Path -LiteralPath $ffmpegSourceArchive -PathType Leaf)) {
    throw 'PIXELS_FFMPEG_SOURCE_ARCHIVE must point to the exact corresponding FFmpeg source ZIP archive.'
}
if ([string]::IsNullOrWhiteSpace($lgplRelinkArchive) -or
    -not (Test-Path -LiteralPath $lgplRelinkArchive -PathType Leaf)) {
    throw 'PIXELS_LGPL_RELINK_ARCHIVE must point to the relinkable Pixels application object ZIP archive.'
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
function Assert-ZipContent {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RequiredPattern,
        [Parameter(Mandatory = $true)][string]$Description
    )
    $archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Path).Path)
    try {
        $files = @($archive.Entries | Where-Object { $_.Length -gt 0 } | ForEach-Object { $_.FullName })
        if ($files.Count -eq 0 -or -not ($files | Where-Object { $_ -match $RequiredPattern } | Select-Object -First 1)) {
            throw "$Description archive does not contain the required files: $Path"
        }
    } finally {
        $archive.Dispose()
    }
}
Assert-ZipContent -Path $ffmpegSourceArchive -RequiredPattern '\.(c|h|S|asm)$' -Description 'FFmpeg corresponding source'
Assert-ZipContent -Path $lgplRelinkArchive -RequiredPattern '\.(o|obj)$' -Description 'LGPL relink'
Assert-ZipContent -Path $lgplRelinkArchive -RequiredPattern '(^|/)(README|RELINK)(\.[^/]*)?$' -Description 'LGPL relink instructions'

$revision = (& git -C $androidRoot rev-parse --short=12 HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
    throw 'Unable to resolve the Git revision for release metadata.'
}
$env:PIXELS_GIT_REVISION = $revision

$tasks = @(':app:lintRelease', 'testDebugUnitTest', ':app:assembleRelease', ':app:bundleRelease', '--stacktrace')
if (-not $SkipClean) {
    $tasks = @('clean') + $tasks
}

Push-Location $androidRoot
try {
    & $gradle @tasks
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle release build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf) -or -not (Test-Path -LiteralPath $bundlePath -PathType Leaf)) {
    throw 'Gradle completed without producing both release APK metadata and AAB output.'
}

$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$element = @($metadata.elements)[0]
$apkPath = Join-Path (Split-Path -Parent $metadataPath) $element.outputFile
if (-not (Test-Path -LiteralPath $apkPath -PathType Leaf)) {
    throw "Release APK is missing: $apkPath"
}

$versionName = [string]$element.versionName
$versionCode = [int]$element.versionCode
if ([string]::IsNullOrWhiteSpace($versionName) -or $versionCode -le 0) {
    throw 'Release output metadata does not contain a valid version.'
}

$artifactRoot = Join-Path $androidRoot "app\apk\release\$versionName"
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
$apkDestination = Join-Path $artifactRoot "Pixels-$versionName-arm64-v8a.apk"
$bundleDestination = Join-Path $artifactRoot "Pixels-$versionName.aab"
Copy-Item -LiteralPath $apkPath -Destination $apkDestination -Force
Copy-Item -LiteralPath $bundlePath -Destination $bundleDestination -Force

$publishedArtifacts = @($apkDestination, $bundleDestination)
$ffmpegSourceDestination = Join-Path $artifactRoot 'ffmpeg-corresponding-source.zip'
$lgplRelinkDestination = Join-Path $artifactRoot 'pixels-lgpl-relink-kit.zip'
$noticesDestination = Join-Path $artifactRoot 'third-party-notices.zip'
Copy-Item -LiteralPath $ffmpegSourceArchive -Destination $ffmpegSourceDestination -Force
Copy-Item -LiteralPath $lgplRelinkArchive -Destination $lgplRelinkDestination -Force
$requiredNoticeFiles = @(
    'open_source_inventory.txt',
    'license_apache_2_0.txt',
    'license_boost_1_0.txt',
    'license_ffmpeg_lgpl_2_1.txt',
    'license_glm.txt',
    'license_leveldb_bsd_3_clause.txt',
    'license_opus_bsd_3_clause.txt',
    'license_protobuf_bsd_3_clause.txt',
    'license_webrtc_bsd_3_clause.txt',
    'license_zlib.txt'
)
$noticeFiles = @($requiredNoticeFiles | ForEach-Object {
    $path = Join-Path $noticeSourceRoot $_
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required third-party notice is missing: $path"
    }
    Get-Item -LiteralPath $path
})
$apkArchive = [System.IO.Compression.ZipFile]::OpenRead($apkDestination)
try {
    $apkEntries = @($apkArchive.Entries | ForEach-Object { $_.FullName.ToLowerInvariant() })
    foreach ($noticeName in $requiredNoticeFiles) {
        $entryName = "res/raw/$noticeName"
        if ($entryName -notin $apkEntries) {
            throw "Release APK does not contain required third-party notice: $entryName"
        }
    }
} finally {
    $apkArchive.Dispose()
}
if (Test-Path -LiteralPath $noticesDestination -PathType Leaf) {
    Remove-Item -LiteralPath $noticesDestination -Force
}
Compress-Archive -LiteralPath $noticeFiles.FullName -DestinationPath $noticesDestination -CompressionLevel Optimal
$publishedArtifacts += @($ffmpegSourceDestination, $lgplRelinkDestination, $noticesDestination)
if (Test-Path -LiteralPath $mappingPath -PathType Leaf) {
    $mappingDestination = Join-Path $artifactRoot 'mapping.txt'
    Copy-Item -LiteralPath $mappingPath -Destination $mappingDestination -Force
    $publishedArtifacts += $mappingDestination
}
$nativeSymbol = Get-ChildItem -LiteralPath (Join-Path $androidRoot 'core-native\build\intermediates\cxx\RelWithDebInfo') -Recurse -File `
    -Filter 'libpixels_android_core.so.dbg' |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if (-not $nativeSymbol) {
    throw 'The release native symbol file was not produced.'
}
$symbolStagingRoot = Join-Path $androidRoot "app\build\intermediates\pixels-native-symbols\$([Guid]::NewGuid().ToString('N'))"
$symbolAbiRoot = Join-Path $symbolStagingRoot 'lib\arm64-v8a'
$symbolsDestination = Join-Path $artifactRoot 'native-debug-symbols.zip'
try {
    New-Item -ItemType Directory -Path $symbolAbiRoot -Force | Out-Null
    Copy-Item -LiteralPath $nativeSymbol.FullName -Destination (Join-Path $symbolAbiRoot 'libpixels_android_core.so')
    if (Test-Path -LiteralPath $symbolsDestination -PathType Leaf) {
        Remove-Item -LiteralPath $symbolsDestination -Force
    }
    Compress-Archive -LiteralPath (Join-Path $symbolStagingRoot 'lib') -DestinationPath $symbolsDestination -CompressionLevel Optimal
} finally {
    if (Test-Path -LiteralPath $symbolStagingRoot -PathType Container) {
        Remove-Item -LiteralPath $symbolStagingRoot -Recurse -Force
    }
}
$publishedArtifacts += $symbolsDestination

$localPropertiesPath = Join-Path $androidRoot 'local.properties'
$sdkRoot = [Environment]::GetEnvironmentVariable('ANDROID_SDK_ROOT')
if ([string]::IsNullOrWhiteSpace($sdkRoot) -and (Test-Path -LiteralPath $localPropertiesPath -PathType Leaf)) {
    $sdkEntry = Get-Content -LiteralPath $localPropertiesPath | Where-Object { $_ -match '^sdk\.dir=' } | Select-Object -First 1
    if ($sdkEntry) {
        $sdkRoot = (($sdkEntry -replace '^sdk\.dir=', '') -replace '\\:', ':') -replace '\\\\', '\'
    }
}
if ([string]::IsNullOrWhiteSpace($sdkRoot)) {
    throw 'ANDROID_SDK_ROOT or sdk.dir in local.properties is required to verify the APK signature.'
}

$apksigner = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'build-tools') -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'apksigner.bat' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $apksigner) {
    throw 'No apksigner.bat was found in the configured Android SDK.'
}

& $apksigner verify --verbose --print-certs $apkDestination
if ($LASTEXITCODE -ne 0) {
    throw 'APK signature verification failed.'
}
$jarsignerOutput = @(& jarsigner -verify $bundleDestination 2>&1)
$jarsignerOutput | Write-Host
if ($LASTEXITCODE -ne 0) {
    throw 'AAB signature verification failed.'
}
$bundleArchive = [System.IO.Compression.ZipFile]::OpenRead($bundleDestination)
try {
    $bundleEntries = @($bundleArchive.Entries | ForEach-Object { $_.FullName.ToUpperInvariant() })
    $hasSignatureFile = @($bundleEntries | Where-Object { $_ -like 'META-INF/*.SF' }).Count -gt 0
    $hasSignatureBlock = @($bundleEntries | Where-Object { $_ -like 'META-INF/*.RSA' -or $_ -like 'META-INF/*.DSA' -or $_ -like 'META-INF/*.EC' }).Count -gt 0
    foreach ($noticeName in $requiredNoticeFiles) {
        $entryName = "BASE/RES/RAW/$($noticeName.ToUpperInvariant())"
        if ($entryName -notin $bundleEntries) {
            throw "Release AAB does not contain required third-party notice: $entryName"
        }
    }
} finally {
    $bundleArchive.Dispose()
}
if (-not $hasSignatureFile -or -not $hasSignatureBlock) {
    throw 'AAB signature metadata is missing.'
}

$artifactMetadata = $publishedArtifacts | ForEach-Object {
    $file = Get-Item -LiteralPath $_
    [ordered]@{
        name = $file.Name
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}
$manifest = [ordered]@{
    product = 'Pixels Android'
    applicationId = [string]$metadata.applicationId
    versionName = $versionName
    versionCode = $versionCode
    abi = 'arm64-v8a'
    gitRevision = $revision
    builtAtUtc = [DateTime]::UtcNow.ToString('o')
    lgpl = [ordered]@{
        ffmpegVersion = '6.1'
        linking = 'static'
        correspondingSource = (Split-Path -Leaf $ffmpegSourceDestination)
        relinkKit = (Split-Path -Leaf $lgplRelinkDestination)
        notices = (Split-Path -Leaf $noticesDestination)
    }
    artifacts = @($artifactMetadata)
}
$manifestPath = Join-Path $artifactRoot 'release-manifest.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Host "Pixels $versionName ($versionCode) release verified."
Write-Host "Artifacts: $artifactRoot"
Get-Content -LiteralPath $manifestPath
