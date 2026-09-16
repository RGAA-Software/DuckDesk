[CmdletBinding()]
param([string]$SdkDirectory = '')
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SdkDirectory)) {
    $SdkDirectory = Join-Path $PSScriptRoot '../.cache/rdp_sdk'
}
function Get-VerifiedFileHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
$sdk = (Resolve-Path -LiteralPath $SdkDirectory).Path
$manifestPath = Join-Path $sdk 'px_rdp_sdk.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$dependencies = Join-Path $PSScriptRoot '../third_party/freerdp/vcpkg.json'
$baseline = (Get-Content -LiteralPath $dependencies -Raw | ConvertFrom-Json).'builtin-baseline'
if ($manifest.schema -ne 2 -or $manifest.freerdp_revision -ne 'aa8650b300aa4cabd85d9c72b431301509b9043f' -or
    $manifest.h264_decoder -ne 'media-foundation' -or $manifest.vcpkg_revision -ne $baseline -or
    $manifest.dependency_manifest_sha256 -ne (Get-VerifiedFileHash $dependencies) -or
    $manifest.decoder_patch_sha256 -ne (Get-VerifiedFileHash (Join-Path $PSScriptRoot '../patches/freerdp/0001-mf-output-state.patch')) -or
    $manifest.output_names_patch_sha256 -ne (Get-VerifiedFileHash (Join-Path $PSScriptRoot '../patches/freerdp/0002-pixels-rdp-output-names.patch'))) {
    throw 'SDK provenance mismatch'
}
foreach ($name in @('px_rdp_winpr', 'px_rdp_core', 'px_rdp_client', 'px_rdp_server', 'px_rdp_server_proxy')) {
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
if ((Get-VerifiedFileHash (Join-Path $sdk 'bin/px_rdp_proxy.exe')) -ne $manifest.proxy_exe_sha256) {
    throw 'SDK proxy executable hash mismatch'
}
$retiredNames = @('freerdp-proxy.exe', 'freerdp-server-proxy3.dll', 'freerdp-server3.dll', 'freerdp-client3.dll',
    'freerdp3.dll', 'winpr3.dll')
foreach ($name in $retiredNames) {
    if (Test-Path -LiteralPath (Join-Path $sdk "bin/$name")) { throw "Retired RDP artifact remains in SDK: $name" }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'vswhere.exe was not found' }
$dumpbin = & $vswhere -latest -products * -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($dumpbin)) { throw 'dumpbin.exe was not found' }
foreach ($name in @('px_rdp_proxy.exe', 'px_rdp_server_proxy.dll', 'px_rdp_server.dll', 'px_rdp_client.dll',
    'px_rdp_core.dll', 'px_rdp_winpr.dll')) {
    $imports = (& $dumpbin /nologo /dependents (Join-Path $sdk "bin/$name") | Out-String)
    if ($imports -match '(?im)^\s*(?:freerdp(?:-client|-server|-server-proxy)?3|winpr3)\.dll\s*$') {
        throw "$name imports a retired RDP DLL name: $($Matches[0].Trim())"
    }
}
Write-Host 'Source-built RDP SDK provenance, product names, PE imports, licenses and runtime hashes verified.'
