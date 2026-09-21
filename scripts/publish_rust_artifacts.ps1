[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('service')]
    [string]$Component,

    [Parameter(Mandatory = $true)]
    [ValidateSet('cloud_node', 'remote')]
    [string]$Product
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$productRoot = Join-Path $repoRoot "build_official\$Product\cargo"
$outputRoot = Join-Path $productRoot 'stage'
$distributionRoot = Join-Path $repoRoot "build_official\$Product\dist"
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
New-Item -ItemType Directory -Path $distributionRoot -Force | Out-Null

$source = Join-Path $productRoot 'target\release\px_service.exe'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Rust service artifact does not exist: $source"
}

function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try {
            return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '')
        } finally {
            $algorithm.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

$expectedHash = Get-Sha256 $source

function Stop-ArtifactProcess([string]$Destination) {
    $resolvedDestination = [IO.Path]::GetFullPath($Destination)
    Get-Process -Name 'px_service' -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            if ([string]::Equals([IO.Path]::GetFullPath($_.Path), $resolvedDestination, [StringComparison]::OrdinalIgnoreCase)) {
                Stop-Process -Id $_.Id -Force -ErrorAction Stop
                $_.WaitForExit(10000)
            }
        } catch [System.ComponentModel.Win32Exception] {
            throw "px_service.exe is in use and its process path cannot be verified: $Destination"
        }
    }
}

function Publish-Artifact([string]$Destination) {
    $temporaryDestination = "$Destination.tmp.$([Guid]::NewGuid().ToString('N'))"
    try {
        Copy-Item -LiteralPath $source -Destination $temporaryDestination -Force
        try {
            Move-Item -LiteralPath $temporaryDestination -Destination $Destination -Force -ErrorAction Stop
        } catch [System.IO.IOException], [System.UnauthorizedAccessException] {
            Stop-ArtifactProcess $Destination
            Move-Item -LiteralPath $temporaryDestination -Destination $Destination -Force -ErrorAction Stop
        }
        $actualHash = Get-Sha256 $Destination
        if ($actualHash -ne $expectedHash) {
            throw "Published px_service.exe hash does not match the Rust build artifact: $Destination"
        }
        Write-Host "HASH OK  $Destination  $actualHash"
    } finally {
        if (Test-Path -LiteralPath $temporaryDestination) {
            Remove-Item -LiteralPath $temporaryDestination -Force
        }
    }
}

Publish-Artifact (Join-Path $outputRoot 'px_service.exe')
Publish-Artifact (Join-Path $distributionRoot 'px_service.exe')
$manifestRefresh = Join-Path $repoRoot 'scripts\refresh_development_dist.py'
& python $manifestRefresh $distributionRoot
if ($LASTEXITCODE -ne 0) {
    throw "development distribution manifest refresh failed with exit code $LASTEXITCODE"
}
