[CmdletBinding()]
param(
    [ValidateSet('cloud_node', 'remote', 'all')]
    [string]$Product = 'all'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build_official'))
$sourceDirectory = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'web\px_web_client\dist'))
$python = (Get-Command python -ErrorAction Stop).Source

if (-not (Test-Path -LiteralPath (Join-Path $sourceDirectory 'index.html') -PathType Leaf)) {
    throw "Build web/px_web_client before publishing development assets: $sourceDirectory"
}

function Get-TreeHashes {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Directory
    )

    $resolvedDirectory = [IO.Path]::GetFullPath($Directory)
    $directoryPrefix = $resolvedDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $hashes = [ordered]@{}
    Get-ChildItem -LiteralPath $resolvedDirectory -Recurse -File | Sort-Object FullName | ForEach-Object {
        if (-not $_.FullName.StartsWith($directoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Web Client artifact escaped its source directory: $($_.FullName)"
        }
        $relativePath = $_.FullName.Substring($directoryPrefix.Length).Replace('\', '/')
        $hashes[$relativePath] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    return $hashes
}

function Assert-TreeHashesEqual {
    param(
        [Parameter(Mandatory = $true)]
        [Collections.IDictionary]$Expected,
        [Parameter(Mandatory = $true)]
        [Collections.IDictionary]$Actual,
        [Parameter(Mandatory = $true)]
        [string]$Context
    )

    $expectedJson = $Expected | ConvertTo-Json -Compress
    $actualJson = $Actual | ConvertTo-Json -Compress
    if ($expectedJson -ne $actualJson) {
        throw "Web Client tree hash mismatch: $Context"
    }
}

function Publish-Tree {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$Destination,
        [Parameter(Mandatory = $true)]
        [Collections.IDictionary]$ExpectedHashes
    )

    $resolvedDestination = [IO.Path]::GetFullPath($Destination)
    if (-not $resolvedDestination.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to publish outside the product build root: $resolvedDestination"
    }
    $destinationParent = Split-Path -Parent $resolvedDestination
    [IO.Directory]::CreateDirectory($destinationParent) | Out-Null
    $stagingDirectory = "$resolvedDestination.staging-$([Guid]::NewGuid().ToString('N'))"
    try {
        [IO.Directory]::CreateDirectory($stagingDirectory) | Out-Null
        Copy-Item -Path (Join-Path $Source '*') -Destination $stagingDirectory -Recurse -Force
        Assert-TreeHashesEqual -Expected $ExpectedHashes -Actual (Get-TreeHashes -Directory $stagingDirectory) -Context $stagingDirectory
        if ([IO.Directory]::Exists($resolvedDestination)) {
            [IO.Directory]::Delete($resolvedDestination, $true)
        } elseif ([IO.File]::Exists($resolvedDestination)) {
            throw "Expected a Web Client directory but found a file: $resolvedDestination"
        }
        [IO.Directory]::Move($stagingDirectory, $resolvedDestination)
    } finally {
        if ([IO.Directory]::Exists($stagingDirectory)) {
            [IO.Directory]::Delete($stagingDirectory, $true)
        }
    }
    Assert-TreeHashesEqual -Expected $ExpectedHashes -Actual (Get-TreeHashes -Directory $resolvedDestination) -Context $resolvedDestination
}

$selectedProducts = if ($Product -eq 'all') { @('cloud_node', 'remote') } else { @($Product) }
$sourceHashes = Get-TreeHashes -Directory $sourceDirectory
if ($sourceHashes.Count -eq 0) {
    throw 'The Web Client production build is empty.'
}

foreach ($selectedProduct in $selectedProducts) {
    $productRoot = [IO.Path]::GetFullPath((Join-Path $buildRoot $selectedProduct))
    if (-not $productRoot.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe product root: $productRoot"
    }
    $distributionDirectory = Join-Path $productRoot 'dist'
    if (-not (Test-Path -LiteralPath (Join-Path $distributionDirectory 'product-manifest.json') -PathType Leaf)) {
        throw "Development product dist is missing: $distributionDirectory"
    }
    Publish-Tree -Source $sourceDirectory -Destination (Join-Path $productRoot 'web') -ExpectedHashes $sourceHashes
    Publish-Tree -Source $sourceDirectory -Destination (Join-Path $distributionDirectory 'web_client') -ExpectedHashes $sourceHashes

    & $python (Join-Path $repositoryRoot 'scripts\refresh_development_dist.py') $distributionDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to refresh the $selectedProduct development manifest."
    }
    & $python (Join-Path $repositoryRoot 'scripts\verify_product_dist.py') $distributionDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to verify the $selectedProduct development dist."
    }
    Assert-TreeHashesEqual -Expected $sourceHashes -Actual (Get-TreeHashes -Directory (Join-Path $distributionDirectory 'web_client')) `
        -Context "$selectedProduct development dist"
    Write-Host "Published Web Client development assets: $selectedProduct ($($sourceHashes.Count) files)"
}
