[CmdletBinding()]
param(
    [switch]$SkipClean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$androidRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $androidRoot '..\..'))
$androidBuildRoot = [Environment]::GetEnvironmentVariable('PIXELS_ANDROID_BUILD_ROOT')
if ([string]::IsNullOrWhiteSpace($androidBuildRoot)) {
    throw 'PIXELS_ANDROID_BUILD_ROOT must be assigned by scripts_build\build_android_product.bat.'
}
$androidNativeRoot = [Environment]::GetEnvironmentVariable('PIXELS_ANDROID_NATIVE_ROOT')
if ([string]::IsNullOrWhiteSpace($androidNativeRoot)) {
    throw 'PIXELS_ANDROID_NATIVE_ROOT must be assigned by scripts_build\build_android_product.bat.'
}
$expectedVersionName = [Environment]::GetEnvironmentVariable('PIXELS_VERSION_NAME')
$expectedVersionCodeText = [Environment]::GetEnvironmentVariable('PIXELS_VERSION_CODE')
$expectedCompany = [Environment]::GetEnvironmentVariable('PIXELS_COMPANY')
$expectedDistribution = [Environment]::GetEnvironmentVariable('PIXELS_DISTRIBUTION')
if ($expectedVersionName -notmatch '^\d+\.\d+\.\d+$' -or $expectedVersionCodeText -notmatch '^\d+$') {
    throw 'Run scripts_build\build_android_product.bat release so the independent Android product version is assigned first.'
}
if ($expectedCompany -ne 'Pixels') {
    throw 'PIXELS_COMPANY must be Pixels and must come from the Android product manifest.'
}
if ($expectedDistribution -notin @('official', 'customer')) {
    throw 'PIXELS_DISTRIBUTION must be official or customer and must come from the Android product build entry point.'
}
$expectedVersionCode = [int]$expectedVersionCodeText
if ($expectedVersionCode -le 0) {
    throw 'PIXELS_VERSION_CODE must be positive.'
}
$gradle = Join-Path $androidRoot 'gradlew.bat'
$metadataPath = Join-Path $androidBuildRoot 'app\outputs\apk\release\output-metadata.json'
$bundlePath = Join-Path $androidBuildRoot 'app\outputs\bundle\release\app-release.aab'
$mappingPath = Join-Path $androidBuildRoot 'app\outputs\mapping\release\mapping.txt'
$propertiesPath = Join-Path $androidRoot 'keystore.properties'
$noticeSourceRoot = Join-Path $androidRoot 'feature-settings\src\main\res\raw'
$vcpkgRoot = [Environment]::GetEnvironmentVariable('VCPKG_ROOT')
if ([string]::IsNullOrWhiteSpace($vcpkgRoot)) {
    $vcpkgRoot = 'C:\source\vcpkg'
}
$ffmpegSpdxPath = Join-Path $vcpkgRoot 'installed\arm64-android\share\ffmpeg\vcpkg.spdx.json'
$ffmpegSourceArchive = Join-Path $vcpkgRoot 'downloads\ffmpeg-ffmpeg-n6.1.tar.gz'

$environmentSigningNames = @(
    'PIXELS_KEYSTORE_FILE',
    'PIXELS_KEYSTORE_PASSWORD',
    'PIXELS_KEY_ALIAS',
    'PIXELS_KEY_PASSWORD',
    'PIXELS_SIGNING_CERT_SHA256'
)
$missingEnvironmentSigning = @($environmentSigningNames | Where-Object {
    [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($_))
})
if ($missingEnvironmentSigning.Count -ne 0 -and -not (Test-Path -LiteralPath $propertiesPath -PathType Leaf)) {
    throw 'Configure all PIXELS_* signing variables or copy keystore.properties.example to the ignored keystore.properties file.'
}

$expectedSigningCertificateSha256 = [Environment]::GetEnvironmentVariable('PIXELS_SIGNING_CERT_SHA256')
if ([string]::IsNullOrWhiteSpace($expectedSigningCertificateSha256) -and
    (Test-Path -LiteralPath $propertiesPath -PathType Leaf)) {
    $certificateProperty = Get-Content -LiteralPath $propertiesPath |
        Where-Object { $_ -match '^\s*certificateSha256\s*=' } |
        Select-Object -First 1
    if ($certificateProperty) {
        $expectedSigningCertificateSha256 = ($certificateProperty -replace '^\s*certificateSha256\s*=\s*', '').Trim()
    }
}
$expectedSigningCertificateSha256 = ([string]$expectedSigningCertificateSha256 -replace '[:\s]', '').ToUpperInvariant()
if ($expectedSigningCertificateSha256 -notmatch '^[0-9A-F]{64}$') {
    throw 'PIXELS_SIGNING_CERT_SHA256 or certificateSha256 must contain the approved 64-hex signing certificate SHA-256.'
}

if (-not (Test-Path -LiteralPath $ffmpegSpdxPath -PathType Leaf)) {
    throw "The installed Android FFmpeg SPDX manifest is missing: $ffmpegSpdxPath"
}
$ffmpegSpdx = Get-Content -LiteralPath $ffmpegSpdxPath -Raw
if ($ffmpegSpdx -notmatch 'git\+https://github\.com/ffmpeg/ffmpeg@n6\.1(?:\"|\s)') {
    throw 'The installed arm64-android FFmpeg dependency is not the approved n6.1 source revision.'
}
if (-not (Test-Path -LiteralPath $ffmpegSourceArchive -PathType Leaf)) {
    throw "The exact FFmpeg n6.1 source archive from the vcpkg cache is missing: $ffmpegSourceArchive"
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

function Assert-TarContent {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RequiredPattern,
        [Parameter(Mandatory = $true)][string]$Description
    )
    $files = @(& tar.exe -tf $Path 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect $Description archive: $Path"
    }
    if ($files.Count -eq 0 -or -not ($files | Where-Object { $_ -match $RequiredPattern } | Select-Object -First 1)) {
        throw "$Description archive does not contain the required files: $Path"
    }
}

function New-LgplRelinkArchive {
    param(
        [Parameter(Mandatory = $true)][string]$NativeRoot,
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [Parameter(Mandatory = $true)][string]$Revision,
        [Parameter(Mandatory = $true)][string]$VersionName
    )

    $objectFiles = @(Get-ChildItem -LiteralPath $NativeRoot -Recurse -File -Filter '*.o')
    if ($objectFiles.Count -eq 0) {
        throw "No Android native object files were produced under $NativeRoot."
    }

    $stagingRoot = Join-Path $androidBuildRoot "staging\pixels-lgpl-relink\$([Guid]::NewGuid().ToString('N'))"
    $objectRoot = Join-Path $stagingRoot 'objects'
    try {
        New-Item -ItemType Directory -Path $objectRoot -Force | Out-Null
        foreach ($objectFile in $objectFiles) {
            $relativePath = [System.IO.Path]::GetRelativePath($NativeRoot, $objectFile.FullName)
            $destination = Join-Path $objectRoot $relativePath
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $objectFile.FullName -Destination $destination
        }

        $buildFiles = @(Get-ChildItem -LiteralPath $NativeRoot -Recurse -File | Where-Object {
            $_.Name -in @('build.ninja', 'CMakeCache.txt', 'compile_commands.json')
        })
        $metadataRoot = Join-Path $stagingRoot 'build-metadata'
        foreach ($buildFile in $buildFiles) {
            $relativePath = [System.IO.Path]::GetRelativePath($NativeRoot, $buildFile.FullName)
            $destination = Join-Path $metadataRoot $relativePath
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $buildFile.FullName -Destination $destination
        }

        $readme = @"
# Pixels Android LGPL relink kit

Product version: $VersionName
Git revision: $Revision
ABI: arm64-v8a
FFmpeg source revision: n6.1

This archive contains the application object files retained from the exact release build, together with its CMake and Ninja metadata. To relink, install the Android NDK and the dependencies recorded by CMakeCache.txt, build a modified LGPL FFmpeg n6.1 for arm64-android with compatible options, replace the FFmpeg static archives referenced by build.ninja, and run the recorded pixels_android_core link command. The resulting libpixels_android_core.so can replace the same ABI library in the APK before the APK is signed again.

The absolute paths in the build metadata describe the original reproducible build environment and may be remapped to another workspace. No Pixels signing key is included.
"@
        Set-Content -LiteralPath (Join-Path $stagingRoot 'README.md') -Value $readme -Encoding utf8

        if (Test-Path -LiteralPath $OutputPath -PathType Leaf) {
            Remove-Item -LiteralPath $OutputPath -Force
        }
        Compress-Archive -Path (Join-Path $stagingRoot '*') -DestinationPath $OutputPath -CompressionLevel Optimal
    } finally {
        if (Test-Path -LiteralPath $stagingRoot -PathType Container) {
            Remove-Item -LiteralPath $stagingRoot -Recurse -Force
        }
    }
}

function Get-ElfBuildId {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ReadElf
    )
    $readElfOutput = @(& $ReadElf --notes $Path 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to read ELF notes from $Path."
    }
    $buildIds = @(
        [regex]::Matches(($readElfOutput -join "`n"), 'Build ID:\s*([0-9a-fA-F]+)') |
            ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() } |
            Select-Object -Unique
    )
    if ($buildIds.Count -ne 1) {
        throw "Expected one ELF Build ID in $Path, found $($buildIds.Count)."
    }
    return $buildIds[0]
}

Assert-TarContent -Path $ffmpegSourceArchive -RequiredPattern '\.(c|h|S|asm)$' -Description 'FFmpeg corresponding source'

$revision = (& git -C $androidRoot rev-parse --short=12 HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($revision)) {
    throw 'Unable to resolve the Git revision for release metadata.'
}
$env:PIXELS_GIT_REVISION = $revision
$env:PIXELS_RELEASE_COMPLIANCE_DRIVER = '1'

$tasks = @(':app:lintRelease', 'testDebugUnitTest', ':app:assembleRelease', ':app:bundleRelease', '--stacktrace')
if (-not $SkipClean) {
    $tasks = @('clean') + $tasks
}
$gradleArguments = @('--project-cache-dir', (Join-Path $androidBuildRoot 'project-cache')) + $tasks

Push-Location $androidRoot
try {
    & $gradle @gradleArguments
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
if ($versionName -ne $expectedVersionName -or $versionCode -ne $expectedVersionCode) {
    throw "Release output version $versionName ($versionCode) does not match Android product version $expectedVersionName ($expectedVersionCode)."
}

$generatedRelinkArchive = Join-Path $androidBuildRoot "staging\pixels-lgpl-relink-$versionName.zip"
New-LgplRelinkArchive -NativeRoot $androidNativeRoot -OutputPath $generatedRelinkArchive -Revision $revision -VersionName $versionName
Assert-ZipContent -Path $generatedRelinkArchive -RequiredPattern '\.(o|obj)$' -Description 'LGPL relink'
Assert-ZipContent -Path $generatedRelinkArchive -RequiredPattern '(^|/)(README|RELINK)(\.[^/]*)?$' -Description 'LGPL relink instructions'

$artifactParent = Join-Path (Split-Path -Parent $androidBuildRoot) 'dist'
$finalArtifactRoot = Join-Path $artifactParent $versionName
if (Test-Path -LiteralPath $finalArtifactRoot) {
    throw "Release $versionName already exists and will not be overwritten: $finalArtifactRoot"
}
$artifactStagingRoot = Join-Path $androidBuildRoot "staging\pixels-release-publish\$([Guid]::NewGuid().ToString('N'))"
$artifactRoot = $artifactStagingRoot
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
$releasePublished = $false
try {
$apkDestination = Join-Path $artifactRoot "Pixels-$versionName-arm64-v8a.apk"
$bundleDestination = Join-Path $artifactRoot "Pixels-$versionName.aab"
Copy-Item -LiteralPath $apkPath -Destination $apkDestination -Force
Copy-Item -LiteralPath $bundlePath -Destination $bundleDestination -Force
$retiredMediaAudit = Join-Path $repositoryRoot 'scripts\audit_android_retired_media.py'
& python $retiredMediaAudit $apkDestination $bundleDestination
if ($LASTEXITCODE -ne 0) {
    throw 'Published Android APK/AAB contains a retired central media artifact or could not be audited.'
}

$publishedArtifacts = @($apkDestination, $bundleDestination)
$ffmpegSourceDestination = Join-Path $artifactRoot 'ffmpeg-corresponding-source.tar.gz'
$lgplRelinkDestination = Join-Path $artifactRoot 'pixels-lgpl-relink-kit.zip'
$noticesDestination = Join-Path $artifactRoot 'third-party-notices.zip'
Copy-Item -LiteralPath $ffmpegSourceArchive -Destination $ffmpegSourceDestination -Force
Copy-Item -LiteralPath $generatedRelinkArchive -Destination $lgplRelinkDestination -Force
Remove-Item -LiteralPath $generatedRelinkArchive -Force
$requiredNoticeFiles = @(
    'open_source_inventory.txt',
    'license_apache_2_0.txt',
    'license_boost_1_0.txt',
    'license_ffmpeg_lgpl_2_1.txt',
    'license_glm.txt',
    'license_leveldb_bsd_3_clause.txt',
    'license_opus_bsd_3_clause.txt',
    'license_protobuf_bsd_3_clause.txt',
    'license_zlib.txt'
)
$noticeFiles = @($requiredNoticeFiles | ForEach-Object {
    $path = Join-Path $noticeSourceRoot $_
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required third-party notice is missing: $path"
    }
    Get-Item -LiteralPath $path
})
$localPropertiesPath = Join-Path $androidRoot 'local.properties'
$sdkRoot = [Environment]::GetEnvironmentVariable('ANDROID_SDK_ROOT')
if ([string]::IsNullOrWhiteSpace($sdkRoot) -and (Test-Path -LiteralPath $localPropertiesPath -PathType Leaf)) {
    $sdkEntry = Get-Content -LiteralPath $localPropertiesPath | Where-Object { $_ -match '^sdk\.dir=' } | Select-Object -First 1
    if ($sdkEntry) {
        $sdkRoot = (($sdkEntry -replace '^sdk\.dir=', '') -replace '\\:', ':') -replace '\\\\', '\'
    }
}
if ([string]::IsNullOrWhiteSpace($sdkRoot)) {
    throw 'ANDROID_SDK_ROOT or sdk.dir in local.properties is required to verify the release artifacts.'
}
$aapt2 = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'build-tools') -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'aapt2.exe' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $aapt2) {
    throw 'No aapt2.exe was found in the configured Android SDK.'
}
$apkResources = @(& $aapt2 dump resources $apkDestination 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect the optimized APK resource table.'
}
foreach ($noticeName in $requiredNoticeFiles) {
    $resourceName = [System.IO.Path]::GetFileNameWithoutExtension($noticeName)
    if (-not ($apkResources | Where-Object { $_ -match "\sraw/$([regex]::Escape($resourceName))\s*$" } | Select-Object -First 1)) {
        throw "Release APK resource table does not contain required third-party notice: raw/$resourceName"
    }
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
$nativeSymbolCandidates = @(Get-ChildItem -LiteralPath (Join-Path $androidBuildRoot 'core-native\intermediates\cxx\RelWithDebInfo') -Recurse -File `
    -Filter 'libpixels_android_core.so.dbg' |
    Sort-Object LastWriteTimeUtc -Descending)
if ($nativeSymbolCandidates.Count -eq 0) {
    throw 'The release native symbol file was not produced.'
}

$apksigner = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'build-tools') -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'apksigner.bat' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $apksigner) {
    throw 'No apksigner.bat was found in the configured Android SDK.'
}

$readElf = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'ndk') -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $readElf) {
    throw 'No llvm-readelf.exe was found in the configured Android NDK.'
}

$symbolStagingRoot = Join-Path $androidBuildRoot "staging\pixels-native-symbols\$([Guid]::NewGuid().ToString('N'))"
$packagedNativePath = Join-Path $symbolStagingRoot 'packaged\libpixels_android_core.so'
$symbolAbiRoot = Join-Path $symbolStagingRoot 'symbols\lib\arm64-v8a'
$symbolsDestination = Join-Path $artifactRoot 'native-debug-symbols.zip'
$packagedNativeBuildId = $null
try {
    New-Item -ItemType Directory -Path (Split-Path -Parent $packagedNativePath) -Force | Out-Null
    New-Item -ItemType Directory -Path $symbolAbiRoot -Force | Out-Null
    $nativeApkArchive = [System.IO.Compression.ZipFile]::OpenRead($apkDestination)
    try {
        $packagedNativeEntry = $nativeApkArchive.GetEntry('lib/arm64-v8a/libpixels_android_core.so')
        if (-not $packagedNativeEntry) {
            throw 'Release APK does not contain arm64-v8a/libpixels_android_core.so.'
        }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($packagedNativeEntry, $packagedNativePath, $true)
    } finally {
        $nativeApkArchive.Dispose()
    }

    $packagedNativeBuildId = Get-ElfBuildId -Path $packagedNativePath -ReadElf $readElf
    $matchingSymbols = @($nativeSymbolCandidates | Where-Object {
        (Get-ElfBuildId -Path $_.FullName -ReadElf $readElf) -eq $packagedNativeBuildId
    })
    if ($matchingSymbols.Count -eq 0) {
        throw "No native symbol file matches packaged ELF Build ID $packagedNativeBuildId."
    }
    $nativeSymbol = $matchingSymbols | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    Copy-Item -LiteralPath $nativeSymbol.FullName -Destination (Join-Path $symbolAbiRoot 'libpixels_android_core.so')
    if (Test-Path -LiteralPath $symbolsDestination -PathType Leaf) {
        Remove-Item -LiteralPath $symbolsDestination -Force
    }
    Compress-Archive -LiteralPath (Join-Path $symbolStagingRoot 'symbols\lib') -DestinationPath $symbolsDestination -CompressionLevel Optimal
} finally {
    if (Test-Path -LiteralPath $symbolStagingRoot -PathType Container) {
        Remove-Item -LiteralPath $symbolStagingRoot -Recurse -Force
    }
}
$publishedArtifacts += $symbolsDestination

$apkSignerOutput = @(& $apksigner verify --verbose --print-certs $apkDestination 2>&1)
$apkSignerExitCode = $LASTEXITCODE
$apkSignerOutput | Write-Host
if ($apkSignerExitCode -ne 0) {
    throw 'APK signature verification failed.'
}
$signingCertificateSha256 = @(
    $apkSignerOutput |
        ForEach-Object { [regex]::Match([string]$_, 'Signer #\d+ certificate SHA-256 digest:\s*([0-9a-fA-F]{64})') } |
        Where-Object Success |
        ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
        Select-Object -Unique
)
if ($signingCertificateSha256.Count -eq 0) {
    throw 'APK signature verification did not report a signer certificate SHA-256 digest.'
}
if ($signingCertificateSha256.Count -ne 1 -or $signingCertificateSha256[0] -ne $expectedSigningCertificateSha256) {
    throw "APK signer certificate does not match the approved SHA-256 $expectedSigningCertificateSha256."
}
$jarsignerOutput = @(& jarsigner -verify $bundleDestination 2>&1)
$jarsignerOutput | Write-Host
if ($LASTEXITCODE -ne 0) {
    throw 'AAB signature verification failed.'
}
$bundleCertificateOutput = @(& keytool -printcert -jarfile $bundleDestination 2>&1)
$bundleCertificateExitCode = $LASTEXITCODE
if ($bundleCertificateExitCode -ne 0) {
    throw 'Unable to read the AAB signing certificate.'
}
$bundleCertificateSha256 = @(
    [regex]::Matches(($bundleCertificateOutput -join "`n"), 'SHA256:\s*((?:[0-9a-fA-F]{2}:){31}[0-9a-fA-F]{2})') |
        ForEach-Object { ($_.Groups[1].Value -replace ':', '').ToUpperInvariant() } |
        Select-Object -Unique
)
if ($bundleCertificateSha256.Count -ne 1 -or $bundleCertificateSha256[0] -ne $expectedSigningCertificateSha256) {
    throw "AAB signer certificate does not match the approved SHA-256 $expectedSigningCertificateSha256."
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
    distribution = $expectedDistribution
    company = $expectedCompany
    applicationId = [string]$metadata.applicationId
    versionName = $versionName
    versionCode = $versionCode
    abi = 'arm64-v8a'
    nativeBuildId = $packagedNativeBuildId
    signingCertificateSha256 = $expectedSigningCertificateSha256
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

New-Item -ItemType Directory -Path $artifactParent -Force | Out-Null
Move-Item -LiteralPath $artifactStagingRoot -Destination $finalArtifactRoot
$releasePublished = $true
$artifactRoot = $finalArtifactRoot
$manifestPath = Join-Path $artifactRoot 'release-manifest.json'
} finally {
    if (-not $releasePublished -and (Test-Path -LiteralPath $artifactStagingRoot -PathType Container)) {
        Remove-Item -LiteralPath $artifactStagingRoot -Recurse -Force
    }
}

Write-Host "Pixels $versionName ($versionCode) release verified."
Write-Host "Artifacts: $artifactRoot"
Get-Content -LiteralPath $manifestPath
