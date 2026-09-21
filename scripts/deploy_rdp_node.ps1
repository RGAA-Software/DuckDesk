[CmdletBinding()]
param(
    [Parameter(Mandatory)][System.Management.Automation.Runspaces.PSSession]$Session,
    [Parameter(Mandatory)][ValidateSet('cloud_node', 'remote')][string]$Product,
    [Parameter(Mandatory)][string]$RenderDist,
    [Parameter(Mandatory)][string]$ServiceExe,
    [string]$FreeRdpBuild,
    [string]$SdkDirectory = '',
    [Parameter(Mandatory)][string]$PolicyDll,
    [Parameter(Mandatory)][string]$ProxyTrustDirectory,
    [Parameter(Mandatory)][string]$ConsoleCaDer,
    [string]$InstallDirectory = ''
)
$ErrorActionPreference = 'Stop'
# Deployment affects only this product's Service/Render executables. It never
# logs on/off a Windows desktop, changes RDS policy or kills a workspace app tree.
if ([string]::IsNullOrWhiteSpace($SdkDirectory)) {
    $SdkDirectory = Join-Path $PSScriptRoot '../.cache/rdp_sdk'
}
$expectedInstallDirectory = if ($Product -eq 'cloud_node') { 'C:\Program Files\Pixels Cloud Node' } else { 'C:\Program Files\Pixels Remote' }
if ([string]::IsNullOrWhiteSpace($InstallDirectory)) {
    $InstallDirectory = $expectedInstallDirectory
}
$rdpInstall = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
if ($rdpInstall -ne $expectedInstallDirectory) {
    throw "The $Product deployment target must be $expectedInstallDirectory; got $rdpInstall"
}
$sdkManifestPath = Join-Path $SdkDirectory 'px_rdp_sdk.json'
$sdkManifest = Get-Content -LiteralPath $sdkManifestPath -Raw | ConvertFrom-Json
$decoderPatch = Join-Path $PSScriptRoot '../patches/freerdp/0001-mf-output-state.patch'
$outputNamesPatch = Join-Path $PSScriptRoot '../patches/freerdp/0002-pixels-rdp-output-names.patch'
if ($sdkManifest.schema -ne 2 -or $sdkManifest.freerdp_revision -ne 'aa8650b300aa4cabd85d9c72b431301509b9043f' -or
    $sdkManifest.h264_decoder -ne 'media-foundation' -or
    $sdkManifest.decoder_patch_sha256 -ne (Get-FileHash -LiteralPath $decoderPatch -Algorithm SHA256).Hash -or
    $sdkManifest.output_names_patch_sha256 -ne (Get-FileHash -LiteralPath $outputNamesPatch -Algorithm SHA256).Hash) {
    throw 'SDK does not match the reviewed FreeRDP base, decoder and patch'
}
foreach ($runtime in $sdkManifest.runtime_sha256.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $SdkDirectory ('bin/' + $runtime.Name)) -Algorithm SHA256).Hash -ne $runtime.Value) {
        throw "SDK runtime hash mismatch: $($runtime.Name)"
    }
}
if ($FreeRdpBuild) {
    foreach ($entry in @{
        'libfreerdp/Release/px_rdp_core.dll' = 'px_rdp_core.dll'
        'client/common/Release/px_rdp_client.dll' = 'px_rdp_client.dll'
        'winpr/libwinpr/Release/px_rdp_winpr.dll' = 'px_rdp_winpr.dll'
    }.GetEnumerator()) {
        if ((Get-FileHash -LiteralPath (Join-Path $FreeRdpBuild $entry.Key) -Algorithm SHA256).Hash -ne
            $sdkManifest.runtime_sha256.($entry.Value)) {
            throw 'Proxy build and Client SDK are from different builds'
        }
    }
    $proxyExe = Join-Path $FreeRdpBuild 'server/proxy/cli/Release/px_rdp_proxy.exe'
    $proxyDll = Join-Path $FreeRdpBuild 'server/proxy/Release/px_rdp_server_proxy.dll'
    $serverDll = Join-Path $FreeRdpBuild 'server/common/Release/px_rdp_server.dll'
} else {
    $proxyExe = Join-Path $SdkDirectory 'bin/px_rdp_proxy.exe'
    $proxyDll = Join-Path $SdkDirectory 'bin/px_rdp_server_proxy.dll'
    $serverDll = Join-Path $SdkDirectory 'bin/px_rdp_server.dll'
    if (-not $sdkManifest.proxy_exe_sha256 -or
        (Get-FileHash -LiteralPath $proxyExe).Hash -ne $sdkManifest.proxy_exe_sha256 -or
        (Get-FileHash -LiteralPath $proxyDll).Hash -ne $sdkManifest.runtime_sha256.'px_rdp_server_proxy.dll') {
        throw 'Complete source-built SDK with matching proxy hashes is required'
    }
}
$rdpFiles = [ordered]@{
    'px_service.exe' = $ServiceExe
    'px_render.exe' = (Join-Path $RenderDist 'px_render.exe')
    'px_render_rtc.dll' = (Join-Path $RenderDist 'px_render_rtc.dll')
    'px_voice_apm.dll' = (Join-Path $RenderDist 'px_voice_apm.dll')
    'rdp\px_rdp_proxy.exe' = $proxyExe
    'rdp\px_rdp_server_proxy.dll' = $proxyDll
    'rdp\px_rdp_server.dll' = $serverDll
    'rdp\proxy\px_rdp_policy.dll' = $PolicyDll
    'rdp\px_rdp_proxy.crt' = (Join-Path $ProxyTrustDirectory 'proxy.crt')
    'rdp\px_rdp_proxy.key' = (Join-Path $ProxyTrustDirectory 'proxy.key')
    'rdp\px_rdp_console_ca.der' = $ConsoleCaDer
}
foreach ($name in @('px_rdp_client.dll', 'px_rdp_core.dll', 'px_rdp_winpr.dll', 'libusb-1.0.dll', 'libssl-3-x64.dll',
    'libcrypto-3-x64.dll', 'zlib1.dll', 'cjson.dll', 'legacy.dll', 'openh264-6.dll')) {
    $rdpFiles["rdp\$name"] = Join-Path $SdkDirectory "bin/$name"
}
$rdpFiles['rdp\px_rdp_sdk.json'] = $sdkManifestPath
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
    if (Test-Path -LiteralPath "$root\px_rdp_deployment.json") {
        $previous = Get-Content -LiteralPath "$root\px_rdp_deployment.json" -Raw | ConvertFrom-Json
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
    Stop-Service -Name px_service -Force -ErrorAction Stop
    $service = Get-Service -Name px_service -ErrorAction Stop
    $service.WaitForStatus(
        [ServiceProcess.ServiceControllerStatus]::Stopped,
        [TimeSpan]::FromSeconds(20))
    $serviceExecutable = [IO.Path]::GetFullPath("$install\px_service.exe")
    Get-CimInstance Win32_Process |
        Where-Object {
            $_.Name -eq 'px_service.exe' -and
            $_.ExecutablePath -and
            [IO.Path]::GetFullPath($_.ExecutablePath).Equals(
                $serviceExecutable,
                [StringComparison]::OrdinalIgnoreCase)
        } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction Stop }
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        $remainingServiceProcesses = @(Get-CimInstance Win32_Process | Where-Object {
                $_.Name -eq 'px_service.exe' -and
                $_.ExecutablePath -and
                [IO.Path]::GetFullPath($_.ExecutablePath).Equals(
                    $serviceExecutable,
                    [StringComparison]::OrdinalIgnoreCase)
            })
        if ($remainingServiceProcesses.Count -eq 0) {
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if ($remainingServiceProcesses.Count -ne 0) {
        throw 'Stopped Service process did not release the installed executable.'
    }
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
        if ($entry.Key -eq 'rdp\px_rdp_proxy.key') { Write-Host 'HASH OK proxy private key (digest not logged)' }
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
        [IO.File]::WriteAllText((Join-Path $install 'rdp\px_rdp_deployment.json'), ($manifest | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        Start-Service -Name px_service
        $manifest | ConvertTo-Json -Compress
    }
    Write-Host "Recoverable previous runtime: $rdpArchive"
} catch {
    Write-Warning "Deployment failed; Service remains stopped to prevent mixing incomplete runtime. Backups: $rdpArchive"
    throw
}
