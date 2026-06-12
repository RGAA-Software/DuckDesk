#Requires -Version 5.1
$ErrorActionPreference = "Continue"

$VcpkgRoot = "C:/source/vcpkg"
$Triplet = "x64-windows-static-release"
$ProxyUrl = "http://127.0.0.1:7890"

$env:HTTP_PROXY  = $ProxyUrl
$env:HTTPS_PROXY = $ProxyUrl
$env:NO_PROXY    = "localhost,127.0.0.1"

# Direct vcpkg dependencies (removed mimalloc/jemalloc; Qt remains official /MD)
$Packages = @(
    "protobuf",
    "gflags",
    "gtest",
    "glm",
    "libvpx",
    "fftw3",
    "openssl",
    "cpr",
    "detours",
    "libjpeg-turbo",
    "zlib",
    "directxmath",
    "sdl2",
    "breakpad",
    "vulkan"
)

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
$Success = @()
$Failed = @()

foreach ($Pkg in $Packages) {
    $Spec = "$Pkg`:$Triplet"
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Installing $Spec" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    & $VcpkgExe install $Spec
    if ($LASTEXITCODE -eq 0) {
        $Success += $Spec
        Write-Host "SUCCESS: $Spec" -ForegroundColor Green
    }
    else {
        $Failed += $Spec
        Write-Host "FAILED: $Spec" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Install summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Succeeded ($($Success.Count)):" -ForegroundColor Green
$Success | ForEach-Object { Write-Host "  $_" -ForegroundColor Green }
Write-Host ""
Write-Host "Failed ($($Failed.Count)):" -ForegroundColor Red
$Failed | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
