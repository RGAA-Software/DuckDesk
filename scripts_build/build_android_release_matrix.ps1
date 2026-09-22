[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$androidRoot = Join-Path $repoRoot 'src\px_android'
$androidOutputRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build_official\android'))
$distributionBuilder = Join-Path $PSScriptRoot 'build_android_product.ps1'
$versionTool = Join-Path $repoRoot 'set_product_version.py'
$python = Get-Command python -ErrorAction Stop

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

function Invoke-DistributionBuilder {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('official', 'customer')]
        [string]$Distribution,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $officialConsoleUrl = [Environment]::GetEnvironmentVariable('PIXELS_OFFICIAL_CONSOLE_URL')
    if ([string]::IsNullOrWhiteSpace($officialConsoleUrl)) {
        throw 'Official and Customer Android release builds require PIXELS_OFFICIAL_CONSOLE_URL.'
    }
    $builderArguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $distributionBuilder,
        '-Distribution', $Distribution, '-Configuration', 'release'
    ) + $Arguments
    Invoke-NativeChecked -FilePath 'powershell.exe' -Arguments $builderArguments
}

function Get-SigningCertificateSha256 {
    $certificateSha256 = [Environment]::GetEnvironmentVariable('PIXELS_SIGNING_CERT_SHA256')
    $propertiesPath = Join-Path $androidRoot 'keystore.properties'
    if ([string]::IsNullOrWhiteSpace($certificateSha256) -and (Test-Path -LiteralPath $propertiesPath -PathType Leaf)) {
        $certificateProperty = Get-Content -LiteralPath $propertiesPath |
            Where-Object { $_ -match '^\s*certificateSha256\s*=' } |
            Select-Object -First 1
        if ($certificateProperty) {
            $certificateSha256 = ($certificateProperty -replace '^\s*certificateSha256\s*=\s*', '').Trim()
        }
    }
    return ([string]$certificateSha256 -replace '[:\s]', '').ToUpperInvariant()
}

function Assert-ReleaseInputs {
    $propertiesPath = Join-Path $androidRoot 'keystore.properties'
    $signingEnvironmentNames = @(
        'PIXELS_KEYSTORE_FILE',
        'PIXELS_KEYSTORE_PASSWORD',
        'PIXELS_KEY_ALIAS',
        'PIXELS_KEY_PASSWORD'
    )
    $hasSigningEnvironment = @($signingEnvironmentNames | Where-Object {
        [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($_))
    }).Count -eq 0
    if (-not $hasSigningEnvironment -and -not (Test-Path -LiteralPath $propertiesPath -PathType Leaf)) {
        throw 'Android release signing inputs must be complete before artifacts are cleaned or a version is consumed.'
    }
    if ($hasSigningEnvironment) {
        $configuredKeystore = [Environment]::GetEnvironmentVariable('PIXELS_KEYSTORE_FILE')
        if (-not (Test-Path -LiteralPath $configuredKeystore -PathType Leaf)) {
            throw "The configured Android release keystore is missing: $configuredKeystore"
        }
    }
    if ((Get-SigningCertificateSha256) -notmatch '^[0-9A-F]{64}$') {
        throw 'The approved Android signing certificate SHA-256 is missing or invalid.'
    }

    $vcpkgRoot = [Environment]::GetEnvironmentVariable('VCPKG_ROOT')
    if ([string]::IsNullOrWhiteSpace($vcpkgRoot)) {
        $vcpkgRoot = 'C:\source\vcpkg'
    }
    foreach ($requiredPath in @(
        (Join-Path $vcpkgRoot 'installed\arm64-android\share\ffmpeg\vcpkg.spdx.json'),
        (Join-Path $vcpkgRoot 'downloads\ffmpeg-ffmpeg-n6.1.tar.gz')
    )) {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Android release compliance input is missing: $requiredPath"
        }
    }
}

Assert-ReleaseInputs
Invoke-DistributionBuilder -Distribution official -Arguments @('-PreflightOnly')
Invoke-DistributionBuilder -Distribution customer -Arguments @('-PreflightOnly')

$expectedBuildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build_official'))
$expectedOutputRoot = [IO.Path]::GetFullPath((Join-Path $expectedBuildRoot 'android'))
if ($androidOutputRoot -ne $expectedOutputRoot -or
    -not $androidOutputRoot.StartsWith($expectedBuildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
    $androidOutputRoot -eq [IO.Path]::GetPathRoot($androidOutputRoot)) {
    throw "Refusing to clean unsafe Android output root: $androidOutputRoot"
}
if ([IO.Directory]::Exists($androidOutputRoot)) {
    [IO.Directory]::Delete($androidOutputRoot, $true)
} elseif ([IO.File]::Exists($androidOutputRoot)) {
    throw "Expected an Android output directory but found a file: $androidOutputRoot"
}
[IO.Directory]::CreateDirectory($androidOutputRoot) | Out-Null

$versionOutput = @(& $python.Source $versionTool --product android --bump --json 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to assign the Android release version: $($versionOutput -join [Environment]::NewLine)"
}
$version = ($versionOutput -join [Environment]::NewLine) | ConvertFrom-Json
if ($version.company -ne 'Pixels') {
    throw "Android product company must be Pixels; got '$($version.company)'."
}
$expectedRevision = (& git -C $repoRoot rev-parse --short=12 HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($expectedRevision)) {
    throw 'Unable to resolve the Git revision for the Android release matrix.'
}

$assignedArguments = @(
    '-AssignedVersionName', [string]$version.product_version,
    '-AssignedVersionCode', [string]$version.product_version_code,
    '-AssignedCompany', [string]$version.company
)
Invoke-DistributionBuilder -Distribution official -Arguments $assignedArguments
Invoke-DistributionBuilder -Distribution customer -Arguments $assignedArguments

$distributionManifests = foreach ($distribution in @('official', 'customer')) {
    $manifestPath = Join-Path $androidOutputRoot "$distribution\dist\$($version.product_version)\release-manifest.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Android $distribution release manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.distribution -ne $distribution -or $manifest.company -ne 'Pixels' -or $manifest.gitRevision -ne $expectedRevision -or
        $manifest.versionName -ne $version.product_version -or [int]$manifest.versionCode -ne [int]$version.product_version_code) {
        throw "Android $distribution release manifest does not match the matrix version."
    }
    [ordered]@{
        distribution = $distribution
        manifest = [IO.Path]::GetRelativePath($androidOutputRoot, $manifestPath).Replace('\', '/')
        sha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    }
}
$matrixManifest = [ordered]@{
    schema_version = 1
    product = 'android'
    company = 'Pixels'
    product_version = [string]$version.product_version
    product_version_code = [int]$version.product_version_code
    git_revision = $expectedRevision
    distributions = @($distributionManifests)
}
$matrixStagingPath = Join-Path $androidOutputRoot '.release-matrix.json.tmp'
$matrixManifestPath = Join-Path $androidOutputRoot 'release-matrix.json'
$matrixManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $matrixStagingPath -Encoding utf8
Move-Item -LiteralPath $matrixStagingPath -Destination $matrixManifestPath -Force

Write-Host "Completed Pixels Android $($version.product_version) official+customer release matrix."
