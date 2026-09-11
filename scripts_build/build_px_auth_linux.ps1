param([string]$Sysroot = "$PSScriptRoot/../.cache/cn-linux-sysroot")
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot/..").Path
$linuxSysroot = (Resolve-Path -LiteralPath $Sysroot).Path
if (-not (Test-Path -LiteralPath "$linuxSysroot/lib/libudev.so")) { throw 'Provide the real target libudev.so in Sysroot/lib; stubs are not supported.' }
New-Item -ItemType Directory -Force -Path "$linuxSysroot/pkgconfig" | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/linux/libudev.pc" -Destination "$linuxSysroot/pkgconfig/libudev.pc"
$env:PATH = "$projectRoot/.tooling/zig;$projectRoot/.tooling/bin;$env:PATH"
$env:PKG_CONFIG_ALLOW_CROSS = '1'
$env:PKG_CONFIG_PATH = "$linuxSysroot/pkgconfig"
$env:PKG_CONFIG_LIBDIR = "$linuxSysroot/pkgconfig"
$env:CARGO_TARGET_DIR = "$projectRoot/.cache/px-auth-linux-target"
Push-Location "$projectRoot/rust_server"
try {
    & "$projectRoot/.tooling/cargo-zigbuild/cargo-zigbuild.exe" zigbuild --locked --release --target x86_64-unknown-linux-gnu -p px_auth_server --bin px_auth
    if ($LASTEXITCODE -ne 0) { throw 'Linux px_auth build failed' }
} finally { Pop-Location }
