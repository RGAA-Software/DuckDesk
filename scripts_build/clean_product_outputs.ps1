[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('cloud_node', 'client', 'remote', 'android', 'all')]
    [string]$Product
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

$targets = if ($Product -eq 'all') {
    @($buildRoot)
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
Write-Host "Product output cleanup complete: $Product"
