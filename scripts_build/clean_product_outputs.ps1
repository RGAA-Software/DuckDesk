[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('cloud_node', 'client', 'remote', 'android', 'all')]
    [string]$Product,

    [ValidateSet('', 'oem')]
    [string]$Distribution = '',

    [string]$OemId = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build_official'))
if (-not $buildRoot.Equals((Join-Path $repoRoot 'build_official'), [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unexpected product build root: $buildRoot"
}
if (-not $buildRoot.StartsWith($repoRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean outside the repository: $buildRoot"
}

if ($Product -eq 'all' -and ($Distribution -or $OemId)) {
    throw 'Distribution-scoped cleanup cannot be combined with Product=all.'
}
if ($Distribution -eq 'oem') {
    if ($Product -eq 'android') {
        throw 'Android OEM cleanup is owned by build_android_product.ps1.'
    }
    if ($OemId -notmatch '^[a-z0-9](?:[a-z0-9-]{1,30}[a-z0-9])$' -or $OemId.Contains('--') -or
        $OemId -in @('pixels', 'official', 'customer', 'oem')) {
        throw 'OEM cleanup requires one canonical, non-reserved OemId.'
    }
} elseif ($OemId) {
    throw 'OemId requires Distribution=oem.'
}

$targets = if ($Product -eq 'all') {
    @($buildRoot)
} elseif ($Distribution -eq 'oem') {
    @([IO.Path]::GetFullPath((Join-Path $buildRoot "$Product\oem\$OemId")))
} else {
    @([IO.Path]::GetFullPath((Join-Path $buildRoot $Product)))
}

foreach ($target in $targets) {
    if ($Product -ne 'all' -and -not $target.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean outside the product build root: $target"
    }
    if ([IO.Directory]::Exists($target)) {
        Write-Host "Removing generated product output: $target"
        [IO.Directory]::Delete($target, $true)
    } elseif ([IO.File]::Exists($target)) {
        throw "Expected a directory but found a file: $target"
    }
}

[IO.Directory]::CreateDirectory($buildRoot) | Out-Null
$cleanupIdentity = if ($Distribution -eq 'oem') { "$Product/oem/$OemId" } else { $Product }
Write-Host "Product output cleanup complete: $cleanupIdentity"
