[CmdletBinding()]
param([string]$SdkDirectory = (Join-Path $PSScriptRoot '../.cache/rdp_sdk'))
$ErrorActionPreference = 'Stop'
function Get-VerifiedFileHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
$sdk = (Resolve-Path -LiteralPath $SdkDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $sdk 'gammaray-rdp-sdk.json') -Raw | ConvertFrom-Json
$dependencies = Join-Path $PSScriptRoot '../third_party/freerdp/vcpkg.json'
$baseline = (Get-Content -LiteralPath $dependencies -Raw | ConvertFrom-Json).'builtin-baseline'
if ($manifest.schema -ne 1 -or $manifest.freerdp_revision -ne 'aa8650b300aa4cabd85d9c72b431301509b9043f' -or
    $manifest.h264_decoder -ne 'media-foundation' -or $manifest.vcpkg_revision -ne $baseline -or
    $manifest.dependency_manifest_sha256 -ne (Get-VerifiedFileHash $dependencies) -or
    $manifest.freerdp_patch_sha256 -ne (Get-VerifiedFileHash (Join-Path $PSScriptRoot '../patches/freerdp/0001-mf-output-state.patch'))) {
    throw 'SDK provenance mismatch'
}
foreach ($name in @('winpr3', 'freerdp3', 'freerdp-client3', 'freerdp-server3', 'freerdp-server-proxy3')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdk "lib/$name.lib")) -or -not $manifest.runtime_sha256.$("$name.dll")) {
        throw "SDK library missing: $name"
    }
}
foreach ($name in @('libusb-1.0.dll', 'libssl-3-x64.dll', 'libcrypto-3-x64.dll', 'zlib1.dll', 'cjson.dll', 'legacy.dll', 'openh264-6.dll')) {
    if (-not $manifest.runtime_sha256.PSObject.Properties[$name]) { throw "SDK dependency missing from manifest: $name" }
}
foreach ($header in @('include/freerdp3/freerdp/freerdp.h', 'include/freerdp3/freerdp/server/proxy/proxy_modules_api.h',
    'include/winpr3/winpr/wtypes.h')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdk $header))) { throw "SDK header missing: $header" }
}
foreach ($license in @('FreeRDP', 'openssl', 'libusb', 'zlib', 'cjson', 'openh264')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdk "licenses/$license-LICENSE"))) { throw "SDK license missing: $license" }
}
foreach ($runtime in $manifest.runtime_sha256.PSObject.Properties) {
    if ((Get-VerifiedFileHash (Join-Path $sdk "bin/$($runtime.Name)")) -ne $runtime.Value) {
        throw "SDK runtime hash mismatch: $($runtime.Name)"
    }
}
if ((Get-VerifiedFileHash (Join-Path $sdk 'bin/freerdp-proxy.exe')) -ne $manifest.proxy_exe_sha256) {
    throw 'SDK proxy executable hash mismatch'
}
Write-Host 'Source-built RDP SDK provenance, headers, libraries, licenses and runtime hashes verified.'
