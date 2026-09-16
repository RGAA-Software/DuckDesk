[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('service')]
    [string]$Component,

    [string]$OutputDir = 'build_official\shared\rust'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDir))
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$source = Join-Path $repoRoot 'rust_client\target\release\px_service.exe'
$destination = Join-Path $outputRoot 'px_service.exe'
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
$temporaryDestination = "$destination.tmp"
Copy-Item -LiteralPath $source -Destination $temporaryDestination -Force
Move-Item -LiteralPath $temporaryDestination -Destination $destination -Force
$actualHash = Get-Sha256 $destination
if ($actualHash -ne $expectedHash) {
    throw 'Published px_service.exe hash does not match the Rust build artifact.'
}
Write-Host "HASH OK  px_service.exe  $actualHash"
