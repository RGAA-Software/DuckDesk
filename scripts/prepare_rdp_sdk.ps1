[CmdletBinding()]
param([string]$FreeRdpSource, [string]$BuildDirectory, [string]$SdkDirectory, [string]$VcpkgDirectory)
$ErrorActionPreference = 'Stop'
function Get-SdkHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $SdkDirectory) { $SdkDirectory = Join-Path $repo '.cache\rdp_sdk' }
$SdkDirectory = [IO.Path]::GetFullPath($SdkDirectory)
if (-not $FreeRdpSource) {
    $FreeRdpSource = Join-Path $repo 'third_party/freerdp/source'
    if (-not (Test-Path -LiteralPath (Join-Path $FreeRdpSource 'CMakeLists.txt'))) {
        & git -C $repo submodule update --init -- third_party/freerdp/source
        if ($LASTEXITCODE -ne 0) { throw 'Pinned FreeRDP submodule initialization failed' }
    }
}
$dependencyManifest = Join-Path $repo 'third_party/freerdp/vcpkg.json'
$vcpkgRevision = (Get-Content -LiteralPath $dependencyManifest -Raw | ConvertFrom-Json).'builtin-baseline'
if (-not $VcpkgDirectory) { $VcpkgDirectory = $env:VCPKG_ROOT }
if (-not $VcpkgDirectory) { $VcpkgDirectory = Join-Path $repo '.cache/rdp_vcpkg' }
$VcpkgDirectory = [IO.Path]::GetFullPath($VcpkgDirectory)
if (-not (Test-Path -LiteralPath $VcpkgDirectory)) {
    & git clone https://github.com/microsoft/vcpkg.git $VcpkgDirectory
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg clone failed' }
    & git -C $VcpkgDirectory checkout --detach $vcpkgRevision
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg pinned checkout failed' }
}
if ((& git -C $VcpkgDirectory rev-parse HEAD) -ne $vcpkgRevision -or $LASTEXITCODE -ne 0) {
    throw 'vcpkg revision differs from the pinned baseline; pass a matching checkout or unset VCPKG_ROOT'
}
if ((& git -C $VcpkgDirectory status --porcelain --untracked-files=no)) { throw 'vcpkg tracked files must be clean' }
if (-not (Test-Path -LiteralPath (Join-Path $VcpkgDirectory 'vcpkg.exe'))) {
    & (Join-Path $VcpkgDirectory 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }
}
$source = (Resolve-Path -LiteralPath $FreeRdpSource).Path
$revision = 'aa8650b300aa4cabd85d9c72b431301509b9043f'
if ((& git -C $source rev-parse HEAD) -ne $revision -or $LASTEXITCODE -ne 0) { throw 'Unexpected FreeRDP revision' }
if ((& git -C $source status --porcelain --untracked-files=no)) { throw 'Pinned FreeRDP source must be clean; no implicit third-party patches' }
# The original checkout stays read-only. A patch-addressed build copy is never
# reset or repaired implicitly: unexpected source changes are a hard failure.
$patch = Join-Path $repo 'patches/freerdp/0001-mf-output-state.patch'
$patchHash = Get-SdkHash $patch
$dependencyHash = Get-SdkHash $dependencyManifest
$buildKey = $revision.Substring(0, 12) + '_' + $patchHash.Substring(0, 12).ToLowerInvariant() + '_' + $dependencyHash.Substring(0, 12).ToLowerInvariant()
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repo ('.cache/rdp_build_' + $buildKey) }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$installed = Join-Path $BuildDirectory 'vcpkg_installed'
$patchedSource = Join-Path $repo ('.cache/rdp_source_' + $revision.Substring(0, 12) + '_' + $patchHash.Substring(0, 12).ToLowerInvariant())
if (-not (Test-Path -LiteralPath $patchedSource)) {
    & git clone --quiet --no-hardlinks $source $patchedSource
    if ($LASTEXITCODE -ne 0) { throw 'Isolated FreeRDP build copy failed' }
    & git -C $patchedSource apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw 'Reviewed FreeRDP patch does not apply to pinned source' }
    & git -C $patchedSource apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'FreeRDP patch application failed' }
}
if ((& git -C $patchedSource rev-parse HEAD) -ne $revision -or $LASTEXITCODE -ne 0) { throw 'Patched FreeRDP base changed' }
$actualPatch = (& git -C $patchedSource diff --binary --no-ext-diff --no-color HEAD --) -join "`n"
if ($LASTEXITCODE -ne 0 -or $actualPatch.TrimEnd() -cne ([IO.File]::ReadAllText($patch).Replace("`r`n", "`n").TrimEnd())) {
    throw 'Isolated FreeRDP build copy differs from the reviewed patch'
}
if ((& git -C $patchedSource ls-files --others --exclude-standard)) { throw 'Unexpected files in patched FreeRDP build copy' }
$source = $patchedSource
& cmake -S $source -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 `
    -U 'ZLIB_LIBRARY_*' `
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CONFIGURATION_TYPES=Release `
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgDirectory/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_MODE=ON `
    "-DVCPKG_MANIFEST_DIR=$repo/third_party/freerdp" "-DVCPKG_INSTALLED_DIR=$installed" -DVCPKG_TARGET_TRIPLET=x64-windows `
    "-DOPENH264_LIBRARY=$installed/x64-windows/lib/openh264.lib" `
    "-DLIBUSB_1_LIBRARY=$installed/x64-windows/lib/libusb-1.0.lib" `
    -DWITH_SERVER=ON -DWITH_PROXY=ON -DWITH_SHADOW=OFF -DWITH_PLATFORM_SERVER=OFF -DWITH_CLIENT=ON `
    -DWITH_CLIENT_SDL=OFF -DWITH_CLIENT_SDL2=OFF -DWITH_CLIENT_SDL3=OFF -DWITH_CLIENT_COMMON=ON -DWITH_CHANNELS=ON -DWITH_CLIENT_CHANNELS=ON -DWITH_SERVER_CHANNELS=ON `
    -DWITH_SAMPLE=OFF -DBUILD_TESTING=OFF -DWITH_MANPAGES=OFF -DWITH_DOCUMENTATION=OFF -DWITH_MEDIA_FOUNDATION=ON `
    -DWITH_FFMPEG=OFF -DWITH_SWSCALE=OFF -DWITH_OPENH264=ON -DWITH_OPUS=OFF -DWITH_JPEG=OFF `
    -DWITH_WINPR_TOOLS=OFF -DWITH_WINPR_TOOLS_CLI=OFF -DWITH_PROXY_MODULES=OFF
if ($LASTEXITCODE -ne 0) { throw 'FreeRDP configure failed' }
& cmake --build $BuildDirectory --config Release --target freerdp-proxy --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'FreeRDP SDK build failed' }
foreach ($component in @('libraries', 'Unspecified')) {
    & cmake --install $BuildDirectory --config Release --prefix $SdkDirectory --component $component
    if ($LASTEXITCODE -ne 0) { throw "FreeRDP install failed: $component" }
}
# Upstream's library install components omit the proxy executable/import library.
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'server/proxy/cli/Release/freerdp-proxy.exe') -Destination (Join-Path $SdkDirectory 'bin') -Force
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'server/proxy/Release/freerdp-server-proxy3.dll') -Destination (Join-Path $SdkDirectory 'bin') -Force
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'server/proxy/Release/freerdp-server-proxy3.lib') -Destination (Join-Path $SdkDirectory 'lib') -Force
foreach ($name in @('libusb-1.0.dll', 'libssl-3-x64.dll', 'libcrypto-3-x64.dll', 'zlib1.dll', 'cjson.dll', 'legacy.dll', 'openh264-6.dll')) {
    Copy-Item -LiteralPath (Join-Path $installed "x64-windows/bin/$name") -Destination (Join-Path $SdkDirectory "bin/$name") -Force
}
$licenses = Join-Path $SdkDirectory 'licenses'
if (-not (Test-Path -LiteralPath $licenses)) { New-Item -ItemType Directory -Path $licenses | Out-Null }
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $licenses 'FreeRDP-LICENSE') -Force
foreach ($package in @('openssl', 'libusb', 'zlib', 'cjson', 'openh264')) {
    $copyright = Join-Path $installed "x64-windows/share/$package/copyright"
    if (Test-Path -LiteralPath $copyright) { Copy-Item -LiteralPath $copyright -Destination (Join-Path $licenses "$package-LICENSE") -Force }
    else { throw "Runtime dependency license missing: $package" }
}
$runtime = @{}
Get-ChildItem -LiteralPath (Join-Path $SdkDirectory 'bin') -File -Filter '*.dll' | ForEach-Object {
    $runtime[$_.Name] = Get-SdkHash $_.FullName
}
$manifest = [ordered]@{ schema = 1; freerdp_revision = $revision; freerdp_patch_sha256 = $patchHash;
    h264_decoder = 'media-foundation'; vcpkg_revision = $vcpkgRevision; dependency_manifest_sha256 = $dependencyHash;
    proxy_exe_sha256 = (Get-SdkHash (Join-Path $SdkDirectory 'bin/freerdp-proxy.exe')); runtime_sha256 = $runtime }
[IO.File]::WriteAllText((Join-Path $SdkDirectory 'gammaray-rdp-sdk.json'), ($manifest | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'verify_rdp_sdk.ps1') -SdkDirectory $SdkDirectory
Write-Host "Pinned SDK installed: $SdkDirectory"
