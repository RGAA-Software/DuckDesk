[CmdletBinding()]
param(
    [Parameter(Mandatory)][System.Management.Automation.Runspaces.PSSession]$Session,
    [Parameter(Mandatory)][string]$RenderDist,
    [Parameter(Mandatory)][string]$ServiceExe,
    [Parameter(Mandatory)][string]$FreeRdpBuild,
    [Parameter(Mandatory)][string]$SdkDirectory,
    [Parameter(Mandatory)][string]$PolicyDll,
    [Parameter(Mandatory)][string]$ProxyTrustDirectory,
    [Parameter(Mandatory)][string]$ConsoleCaDer,
    [string]$InstallDirectory = 'C:\Program Files\PixelsRender'
)
$ErrorActionPreference = 'Stop'
# Deployment affects only this product's Service/Render executables. It never
# logs on/off a Windows desktop, changes RDS policy or kills a workspace app tree.
$rdpInstall = [IO.Path]::GetFullPath($InstallDirectory)
if ($rdpInstall -ne 'C:\Program Files\PixelsRender') { throw 'This deployment entry point requires the documented product installation' }
$sdkManifest = Get-Content -LiteralPath (Join-Path $SdkDirectory 'gammaray-rdp-sdk.json') -Raw | ConvertFrom-Json
$reviewedPatch = Join-Path $PSScriptRoot '../patches/freerdp/0001-mf-output-state.patch'
if ($sdkManifest.schema -ne 1 -or $sdkManifest.freerdp_revision -ne 'aa8650b300aa4cabd85d9c72b431301509b9043f' -or
    $sdkManifest.h264_decoder -ne 'media-foundation' -or
    $sdkManifest.freerdp_patch_sha256 -ne (Get-FileHash -LiteralPath $reviewedPatch -Algorithm SHA256).Hash) {
    throw 'SDK does not match the reviewed FreeRDP base, decoder and patch'
}
foreach ($runtime in $sdkManifest.runtime_sha256.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $SdkDirectory ('bin/' + $runtime.Name)) -Algorithm SHA256).Hash -ne $runtime.Value) {
        throw "SDK runtime hash mismatch: $($runtime.Name)"
    }
}
foreach ($entry in @{
    'libfreerdp/Release/freerdp3.dll' = 'freerdp3.dll'
    'client/common/Release/freerdp-client3.dll' = 'freerdp-client3.dll'
    'winpr/libwinpr/Release/winpr3.dll' = 'winpr3.dll'
}.GetEnumerator()) {
    if ((Get-FileHash -LiteralPath (Join-Path $FreeRdpBuild $entry.Key) -Algorithm SHA256).Hash -ne
        $sdkManifest.runtime_sha256.($entry.Value)) {
        throw 'Proxy build and Client SDK are from different builds'
    }
}
$rdpFiles = [ordered]@{
    'px_service.exe' = $ServiceExe
    'px_render.exe' = (Join-Path $RenderDist 'px_render.exe')
    'px_render_rtc.dll' = (Join-Path $RenderDist 'px_render_rtc.dll')
    'px_render_rtc_remote.dll' = (Join-Path $RenderDist 'px_render_rtc_remote.dll')
    'px_voice_apm.dll' = (Join-Path $RenderDist 'px_voice_apm.dll')
    'rdp\freerdp-proxy.exe' = (Join-Path $FreeRdpBuild 'server/proxy/cli/Release/freerdp-proxy.exe')
    'rdp\freerdp-server-proxy3.dll' = (Join-Path $FreeRdpBuild 'server/proxy/Release/freerdp-server-proxy3.dll')
    'rdp\freerdp-server3.dll' = (Join-Path $FreeRdpBuild 'server/common/Release/freerdp-server3.dll')
    'rdp\proxy\proxy-gammaray-policy-plugin.dll' = $PolicyDll
    'rdp\proxy.crt' = (Join-Path $ProxyTrustDirectory 'proxy.crt')
    'rdp\proxy.key' = (Join-Path $ProxyTrustDirectory 'proxy.key')
    'rdp\console-ca.der' = $ConsoleCaDer
}
foreach ($name in @('freerdp-client3.dll', 'freerdp3.dll', 'winpr3.dll', 'libusb-1.0.dll', 'libssl-3-x64.dll',
    'libcrypto-3-x64.dll', 'zlib1.dll', 'cjson.dll', 'legacy.dll', 'openh264-6.dll')) {
    $rdpFiles["rdp\$name"] = Join-Path $SdkDirectory "bin/$name"
}
$rdpFiles['rdp\sdk.json'] = Join-Path $SdkDirectory 'gammaray-rdp-sdk.json'
foreach ($name in @('FreeRDP-LICENSE', 'openssl-LICENSE', 'libusb-LICENSE', 'zlib-LICENSE', 'cjson-LICENSE', 'openh264-LICENSE')) {
    $rdpFiles["rdp\licenses\$name"] = Join-Path $SdkDirectory "licenses/$name"
}
$rdpHashes = @{}
foreach ($entry in $rdpFiles.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) { throw "Missing runtime artifact: $($entry.Key)" }
    $rdpHashes[$entry.Key] = (Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash
}
$proxyCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::CreateFromPem(
    [IO.File]::ReadAllText((Join-Path $ProxyTrustDirectory 'proxy.crt')))
$proxyPin = $proxyCertificate.GetCertHashString([Security.Cryptography.HashAlgorithmName]::SHA256)
$proxyCertificate.Dispose()
$rdpArchive = Invoke-Command -Session $Session -ArgumentList $rdpInstall, $proxyPin -ScriptBlock {
    param($install, $pin)
    $ErrorActionPreference = 'Stop'
    if (-not (Test-Path -LiteralPath "$install\px_service.exe")) { throw 'Product Service is not installed' }
    if ((Get-Item -LiteralPath $install).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse installation rejected' }
    $root = Join-Path $install 'rdp'
    if (Test-Path -LiteralPath "$root\deployment.json") {
        $previous = Get-Content -LiteralPath "$root\deployment.json" -Raw | ConvertFrom-Json
        if ($previous.proxy_certificate_sha256 -ne $pin) { throw 'Refusing implicit proxy identity rotation' }
    }
    $active = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'px_render.exe' -and $_.CommandLine -match '--app_mode=rdp' })
    if ($active.Count -ne 0) { throw 'Disconnect the active RDP workspace through Console before node deployment' }
    $backup = Join-Path $install ('rdp-upgrade-backup-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    foreach ($path in @($root, "$root\proxy", "$root\licenses", $backup)) {
        if (-not (Test-Path -LiteralPath $path)) { New-Item -ItemType Directory -Path $path | Out-Null }
        if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse runtime rejected' }
        $acl = [Security.AccessControl.DirectorySecurity]::new()
        $acl.SetAccessRuleProtection($true, $false)
        foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
            $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),
                'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
        }
        Set-Acl -LiteralPath $path -AclObject $acl
    }
    Stop-Service -Name px_service -ErrorAction Stop
    foreach ($process in @(Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'px_render.exe' -and $_.ExecutablePath -eq "$install\px_render.exe" })) {
        # Stop only the host Render process, not its descendants or RDS sessions.
        try { Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop }
        catch {
            # Stopping the parent can make its CEF subprocesses exit before
            # this snapshot is traversed. Already gone is a successful stop.
            if (Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue) { throw }
        }
    }
    $backup
}
try {
    foreach ($entry in $rdpFiles.GetEnumerator()) {
        $destination = Join-Path $rdpInstall $entry.Key
        Invoke-Command -Session $Session -ArgumentList $destination, $rdpArchive, $entry.Key -ScriptBlock {
            param($file, $archive, $relative)
            if (Test-Path -LiteralPath $file -PathType Leaf) {
                $backupFile = Join-Path $archive $relative
                $parent = Split-Path $backupFile -Parent
                if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
                Copy-Item -LiteralPath $file -Destination $backupFile
            }
        }
        Copy-Item -LiteralPath $entry.Value -Destination $destination -ToSession $Session -Force
        $actual = Invoke-Command -Session $Session -ArgumentList $destination -ScriptBlock {
            param($file) (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
        }
        if ($actual -ne $rdpHashes[$entry.Key]) { throw "Remote artifact hash mismatch: $($entry.Key)" }
        if ($entry.Key -eq 'rdp\proxy.key') { Write-Host 'HASH OK proxy private key (digest not logged)' }
        else { Write-Host "HASH OK $($entry.Key) $actual" }
    }
    Invoke-Command -Session $Session -ArgumentList $rdpInstall, $proxyPin -ScriptBlock {
        param($install, $pin)
        $ErrorActionPreference = 'Stop'
        $certificates = @(Get-ChildItem 'Cert:\LocalMachine\Remote Desktop')
        if ($certificates.Count -ne 1) { throw 'RDS certificate selection requires an explicit deployment decision' }
        $hash = [Security.Cryptography.SHA256]::Create()
        try { $targetPin = [BitConverter]::ToString($hash.ComputeHash($certificates[0].RawData)).Replace('-', '') }
        finally { $hash.Dispose() }
        $manifest = [ordered]@{ schema = 1; freerdp_revision = 'aa8650b300aa4cabd85d9c72b431301509b9043f';
            target_domain = $env:COMPUTERNAME; target_certificate_sha256 = $targetPin; proxy_certificate_sha256 = $pin }
        [IO.File]::WriteAllText((Join-Path $install 'rdp\deployment.json'), ($manifest | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        Start-Service -Name px_service
        $manifest | ConvertTo-Json -Compress
    }
    Write-Host "Recoverable previous runtime: $rdpArchive"
} catch {
    Write-Warning "Deployment failed; Service remains stopped to prevent mixing incomplete runtime. Backups: $rdpArchive"
    throw
}
