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

$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo "build_official/src/px_deps/px_panel_imgui.exe"
$destinationDirectory = Join-Path $repo "build_official/dist"
$destination = Join-Path $destinationDirectory "px_panel_imgui.exe"
$fontSource = Join-Path $repo "build_official/src/px_deps/resources/fonts/Roboto-Regular.ttf"
$fontDestination = Join-Path $destinationDirectory "resources/fonts/Roboto-Regular.ttf"

New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $fontDestination) | Out-Null
Get-Process -Name "px_panel_imgui" -ErrorAction SilentlyContinue | Stop-Process -Force
Copy-Item -LiteralPath $source -Destination $destination -Force
Copy-Item -LiteralPath $fontSource -Destination $fontDestination -Force

$sourceHash = Get-Sha256 $source
$destinationHash = Get-Sha256 $destination
if ($sourceHash -ne $destinationHash) {
    throw "px_panel_imgui.exe publish hash mismatch"
}

Write-Host "Published px_panel_imgui.exe"
Write-Host "SHA-256: $sourceHash"
