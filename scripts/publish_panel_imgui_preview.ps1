param(
    [string]$BuildDirectory = "build_official"
)

$ErrorActionPreference = "Stop"

function Get-Sha256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace("-", "")
    }
    finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$source = Join-Path $repoRoot "$BuildDirectory\src\px_deps\px_panel_imgui_preview.exe"
$destinationDirectory = Join-Path $repoRoot "build_official\dist"
$destination = Join-Path $destinationDirectory "px_panel_imgui_preview.exe"
$fontSource = Join-Path $repoRoot "$BuildDirectory\src\px_deps\resources\fonts\Roboto-Regular.ttf"
$fontDestination = Join-Path $destinationDirectory "resources\fonts\Roboto-Regular.ttf"

if (-not (Test-Path -LiteralPath $source)) {
    throw "Preview executable was not produced: $source"
}

New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $fontDestination) | Out-Null
Get-Process -Name "px_panel_imgui_preview" -ErrorAction SilentlyContinue | Stop-Process -Force
Copy-Item -LiteralPath $source -Destination $destination -Force
Copy-Item -LiteralPath $fontSource -Destination $fontDestination -Force

$sourceHash = Get-Sha256 $source
$destinationHash = Get-Sha256 $destination
if ($sourceHash -ne $destinationHash) {
    throw "Preview artifact hash mismatch after publish."
}
$fontSourceHash = Get-Sha256 $fontSource
$fontDestinationHash = Get-Sha256 $fontDestination
if ($fontSourceHash -ne $fontDestinationHash) {
    throw "Preview font hash mismatch after publish."
}

Write-Host "Published px_panel_imgui_preview.exe"
Write-Host "SHA-256: $sourceHash"
Write-Host "Font SHA-256: $fontSourceHash"
