param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("render", "client", "panel", "render_network_libraries", "render_network_library", "hook_audio", "ft_protocol")]
    [string]$Component,
    [string]$LibraryTarget = "",
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [Parameter(Mandatory = $true)]
    [string]$DistDir
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$distRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $DistDir))
$cachePath = Join-Path $buildRoot "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw "CMake cache does not exist: $cachePath"
}
$productEntry = Select-String -LiteralPath $cachePath -Pattern '^PX_PRODUCT:STRING=(cloud_node|client|remote)$'
if (-not $productEntry) { throw "PX_PRODUCT is missing or invalid in $cachePath" }
$product = $productEntry.Matches[0].Groups[1].Value
$expectedBuildRoot = Join-Path $repoRoot "build_official\$product\cmake"
$expectedDistRoot = Join-Path $repoRoot "build_official\$product\dist"
if (-not $buildRoot.Equals($expectedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildDir must be the isolated $product CMake directory: $expectedBuildRoot"
}
if (-not $distRoot.Equals($expectedDistRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "DistDir must be the isolated $product runtime directory: $expectedDistRoot"
}
$restartRenderService = $false

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null

function Stop-RenderServiceForPublish {
    $service = Get-Service -Name "px_service" -ErrorAction SilentlyContinue
    if ($service -and $service.Status -eq [ServiceProcess.ServiceControllerStatus]::Running) {
        Write-Host "Stopping px_service while publishing Render artifacts."
        Stop-Service -Name "px_service" -Force
        (Get-Service -Name "px_service").WaitForStatus(
            [ServiceProcess.ServiceControllerStatus]::Stopped,
            [TimeSpan]::FromSeconds(15))
        $script:restartRenderService = $true
    }
    $renderProcesses = Get-Process -Name "px_render" -ErrorAction SilentlyContinue
    if ($renderProcesses) {
        $renderProcesses | Stop-Process -Force
        $renderProcesses | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
    }
}

function Restore-RenderServiceAfterPublish {
    if (-not $script:restartRenderService) {
        return
    }
    Write-Host "Restarting px_service after Render artifact publication."
    Start-Service -Name "px_service"
    (Get-Service -Name "px_service").WaitForStatus(
        [ServiceProcess.ServiceControllerStatus]::Running,
        [TimeSpan]::FromSeconds(15))
    $script:restartRenderService = $false
}

function Move-RetiredClientRtcRuntime {
    $retiredRtc = Join-Path $distRoot "px_client_rtc.dll"
    if (-not (Test-Path -LiteralPath $retiredRtc -PathType Leaf)) { return }
    # Preserve old runtime bytes outside dist; never replace an earlier archive.
    $retiredDirectory = Join-Path $buildRoot ("retired-client-runtime/" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $retiredDirectory | Out-Null
    $retiredHash = Get-Sha256Hex -Path $retiredRtc
    $retiredDestination = Join-Path $retiredDirectory "px_client_rtc.dll"
    Move-Item -LiteralPath $retiredRtc -Destination $retiredDestination
    if ((Get-Sha256Hex -Path $retiredDestination) -ne $retiredHash) { throw 'Retired RTC runtime archive hash mismatch.' }
    Write-Host "ARCHIVED retired Native RTC DLL: $retiredDestination"
}

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][string]$Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace("-", "")
    }
    finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}

function Publish-VerifiedFile {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [string]$ProcessName = ""
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "build artifact does not exist: $Source"
    }
    $destinationDirectory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
    $sourceHash = Get-Sha256Hex -Path $Source
    if ((Test-Path -LiteralPath $Destination -PathType Leaf) -and (Get-Sha256Hex -Path $Destination) -eq $sourceHash) {
        Write-Host "HASH OK  $($Destination.Substring($distRoot.Length + 1))  $sourceHash"
        return
    }
    try {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
    }
    catch {
        if ([string]::IsNullOrWhiteSpace($ProcessName)) {
            throw
        }
        if ($ProcessName -eq "px_render") {
            Stop-RenderServiceForPublish
        }
        $running = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
        if (-not $running -and $ProcessName -ne "px_render") {
            throw
        }
        if ($running) {
            Write-Host "Stopping $ProcessName because its runtime artifact is in use."
            $running | Stop-Process -Force
            $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
        }
        $copied = $false
        for ($attempt = 1; $attempt -le 20; ++$attempt) {
            try {
                Copy-Item -LiteralPath $Source -Destination $Destination -Force
                $copied = $true
                break
            }
            catch {
                if ($attempt -eq 20) {
                    throw
                }
                Start-Sleep -Milliseconds 100
            }
        }
        if (-not $copied) {
            throw "failed to publish artifact after retry: $Destination"
        }
    }

    $destinationHash = Get-Sha256Hex -Path $Destination
    if ($sourceHash -ne $destinationHash) {
        throw "SHA-256 mismatch after publish: $Destination"
    }
    $relative = $Destination.Substring($distRoot.Length).TrimStart([char]'\')
    Write-Host "HASH OK  $relative  $destinationHash"
}

function Publish-LanguageResources {
    $sourceRoot = Join-Path $repoRoot "src\px_ui\resources\language"
    $destinationRoot = Join-Path $distRoot "resources\language"
    Get-ChildItem -LiteralPath $sourceRoot -File -Recurse | ForEach-Object {
        $relative = $_.FullName.Substring($sourceRoot.Length).TrimStart([char]'\')
        Publish-VerifiedFile -Source $_.FullName -Destination (Join-Path $destinationRoot $relative)
    }
}

function Publish-DesktopBrandLogo {
    param([Parameter(Mandatory = $true)][string]$ProcessName)
    Publish-VerifiedFile `
        -Source (Join-Path $buildRoot "src\px_deps\resources\icons\px_icon.png") `
        -Destination (Join-Path $distRoot "resources\icons\px_icon.png") `
        -ProcessName $ProcessName
    Publish-VerifiedFile `
        -Source (Join-Path $buildRoot "src\px_deps\resources\fonts\Roboto-Medium.ttf") `
        -Destination (Join-Path $distRoot "resources\fonts\Roboto-Medium.ttf") `
        -ProcessName $ProcessName
    foreach ($name in @('px_icon.png', 'px_icon-150.png', 'px_icon-200.png')) {
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot ('src\px_deps\resources\icons\brand\' + $name)) `
            -Destination (Join-Path $distRoot ('resources\icons\brand\' + $name)) `
            -ProcessName $ProcessName
    }
    Publish-VerifiedFile `
        -Source (Join-Path $buildRoot 'src\px_deps\resources\licenses\FreeType.txt') `
        -Destination (Join-Path $distRoot 'resources\licenses\FreeType.txt') `
        -ProcessName $ProcessName
}

function Publish-DesktopPlatformIcons {
    param([Parameter(Mandatory = $true)][string]$ProcessName)
    foreach ($name in @(
        'windows.png', 'windows-150.png', 'windows-200.png',
        'macos.png', 'macos-150.png', 'macos-200.png',
        'android.png', 'android-150.png', 'android-200.png',
        'ios.png', 'ios-150.png', 'ios-200.png')) {
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot ('src\px_deps\resources\icons\platform\' + $name)) `
            -Destination (Join-Path $distRoot ('resources\icons\platform\' + $name)) `
            -ProcessName $ProcessName
    }
    Publish-VerifiedFile `
        -Source (Join-Path $buildRoot 'src\px_deps\resources\licenses\Tabler.txt') `
        -Destination (Join-Path $distRoot 'resources\licenses\Tabler.txt') `
        -ProcessName $ProcessName
}

function Remove-RetiredQtArtifacts {
    $distFullPath = [IO.Path]::GetFullPath($distRoot).TrimEnd([char]'\')
    foreach ($file in Get-ChildItem -LiteralPath $distRoot -File -ErrorAction SilentlyContinue) {
        if ($file.Name -notmatch '(?i)^Qt[56].*\.dll$' -and $file.Name -notin @('skin_official.dll', 'skin_opensource.dll')) {
            continue
        }
        if (-not $file.FullName.StartsWith($distFullPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove artifact outside dist: $($file.FullName)"
        }
        Remove-Item -LiteralPath $file.FullName -Force
        Write-Host "REMOVED retired Qt runtime $($file.FullName)"
    }
    foreach ($relativePath in @('generic', 'iconengines', 'imageformats', 'networkinformation', 'platforms', 'styles', 'tls', 'deps\theme')) {
        $retiredPath = [IO.Path]::GetFullPath((Join-Path $distRoot $relativePath))
        if (-not $retiredPath.StartsWith($distFullPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to remove directory outside dist: $retiredPath"
        }
        if (Test-Path -LiteralPath $retiredPath -PathType Container) {
            Remove-Item -LiteralPath $retiredPath -Recurse -Force
            Write-Host "REMOVED retired Qt directory $retiredPath"
        }
    }
    $retiredClientPlugin = Join-Path $distRoot 'deps\ct_plugins\multi_screens.dll'
    if (Test-Path -LiteralPath $retiredClientPlugin -PathType Leaf) {
        Remove-Item -LiteralPath $retiredClientPlugin -Force
        Write-Host "REMOVED retired Qt Client plug-in $retiredClientPlugin"
    }
}

function Remove-RetiredPanelArtifacts {
    foreach ($name in @("px_panel_imgui.exe", "px_panel_imgui_preview.exe", "px_panel_product_tests.exe")) {
        $retiredPath = Join-Path $distRoot $name
        if (-not (Test-Path -LiteralPath $retiredPath -PathType Leaf)) {
            continue
        }
        Remove-Item -LiteralPath $retiredPath -Force
        Write-Host "REMOVED retired Panel artifact $retiredPath"
    }
}

function Remove-RetiredClientRecordingCore {
    $stalePath = Join-Path $distRoot "px_client_recording_core.dll"
    if (-not (Test-Path -LiteralPath $stalePath -PathType Leaf)) {
        return
    }
    try {
        Remove-Item -LiteralPath $stalePath -Force
    }
    catch {
        $running = Get-Process -Name "px_client" -ErrorAction SilentlyContinue
        if (-not $running) {
            throw
        }
        Write-Host "Stopping px_client because the retired recording core is in use."
        $running | Stop-Process -Force
        $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $stalePath -Force
    }
    Write-Host "REMOVED retired Client recording core $stalePath"
}

function Remove-LegacyRenderPluginDirectory {
    $legacyDirectory = Join-Path $distRoot "deps\rd_plugins"
    if (-not (Test-Path -LiteralPath $legacyDirectory -PathType Container)) {
        return
    }
    try {
        Remove-Item -LiteralPath $legacyDirectory -Recurse -Force
    }
    catch {
        Stop-RenderServiceForPublish
        Remove-Item -LiteralPath $legacyDirectory -Recurse -Force
    }
    Write-Host "REMOVED legacy Render plugin directory $legacyDirectory"
}

function Remove-RetiredRenderNetworkLibraries {
    foreach ($relativePath in @(
        "net_rtc.dll",
        "net_rtc_local.dll",
        "deps\network\net_rtc.dll",
        "deps\network\net_rtc_local.dll",
        "deps\network\px_render_rtc.dll",
        "deps\network\px_render_rtc_remote.dll",
        "px_render_rtc_remote.dll")) {
        $retiredPath = Join-Path $distRoot $relativePath
        if (-not (Test-Path -LiteralPath $retiredPath -PathType Leaf)) {
            continue
        }
        try {
            Remove-Item -LiteralPath $retiredPath -Force
        }
        catch {
            Stop-RenderServiceForPublish
            Remove-Item -LiteralPath $retiredPath -Force
        }
        Write-Host "REMOVED retired Render network library $retiredPath"
    }
    $retiredDirectory = Join-Path $distRoot "deps\network"
    if ((Test-Path -LiteralPath $retiredDirectory -PathType Container) -and -not (Get-ChildItem -LiteralPath $retiredDirectory -Force)) {
        Remove-Item -LiteralPath $retiredDirectory
        Write-Host "REMOVED empty retired Render network directory $retiredDirectory"
    }
}

$renderNetworkLibraryMap = @{
    "net_rtc_local" = "network\webrtc\local\px_render_rtc.dll"
}

function Publish-RenderNetworkLibrary {
    param([Parameter(Mandatory = $true)][string]$Target)
    if (-not $renderNetworkLibraryMap.ContainsKey($Target)) {
        throw "unknown Render network library target: $Target"
    }
    $relativeSource = $renderNetworkLibraryMap[$Target]
    $source = Join-Path $buildRoot ("src\px_render\" + $relativeSource)
    $destination = Join-Path $distRoot (Split-Path -Leaf $relativeSource)
    Publish-VerifiedFile -Source $source -Destination $destination -ProcessName "px_render"
}

function Publish-RenderCefRuntime {
    $cachePath = Join-Path $buildRoot "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        throw "CMake cache does not exist: $cachePath"
    }
    $productEntry = Select-String -LiteralPath $cachePath -Pattern '^PX_PRODUCT:STRING=(.+)$'
    if (-not $productEntry) {
        throw "PX_PRODUCT is missing from CMake cache: $cachePath"
    }
    $buildProduct = $productEntry.Matches[0].Groups[1].Value
    if ($buildProduct -ne "cloud_node") {
        Write-Host "SKIP CEF runtime for PX_PRODUCT=$buildProduct"
        return
    }

    $renderBuildDirectory = Join-Path $buildRoot "src\px_render"
    $runtimeFiles = @(
        "chrome_elf.dll",
        "d3dcompiler_47.dll",
        "dxcompiler.dll",
        "dxil.dll",
        "libcef.dll",
        "libEGL.dll",
        "libGLESv2.dll",
        "v8_context_snapshot.bin",
        "vk_swiftshader.dll",
        "vk_swiftshader_icd.json",
        "vulkan-1.dll",
        "chrome_100_percent.pak",
        "chrome_200_percent.pak",
        "icudtl.dat",
        "resources.pak"
    )

    foreach ($name in $runtimeFiles) {
        Publish-VerifiedFile -Source (Join-Path $renderBuildDirectory $name) `
            -Destination (Join-Path $distRoot $name) -ProcessName "px_render"
    }

    $localeSource = Join-Path $renderBuildDirectory "locales"
    if (-not (Test-Path -LiteralPath $localeSource -PathType Container)) {
        throw "CEF locales were not staged beside px_render.exe: $localeSource"
    }

    Get-ChildItem -LiteralPath $localeSource -File -Recurse | ForEach-Object {
        $relative = $_.FullName.Substring($localeSource.Length).TrimStart('\')
        Publish-VerifiedFile -Source $_.FullName -Destination (Join-Path $distRoot (Join-Path "locales" $relative)) `
            -ProcessName "px_render"
    }
}

try {
switch ($Component) {
    "render" {
        Remove-LegacyRenderPluginDirectory
        Remove-RetiredRenderNetworkLibraries
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_render\px_render.exe") `
            -Destination (Join-Path $distRoot "px_render.exe") `
            -ProcessName "px_render"
        Publish-VerifiedFile `
            -Source (Join-Path $repoRoot "src\px_render\architecture\processors\frame_carrier\resources\ic_logo_point.png") `
            -Destination (Join-Path $distRoot "resources\render\frame_carrier\ic_logo_point.png") `
            -ProcessName "px_render"
        foreach ($target in $renderNetworkLibraryMap.Keys | Sort-Object) {
            Publish-RenderNetworkLibrary -Target $target
        }
        Publish-RenderCefRuntime
    }
    "client" {
        Remove-RetiredQtArtifacts
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\px_client.exe") `
            -Destination (Join-Path $distRoot "px_client.exe") `
            -ProcessName "px_client"
        Move-RetiredClientRtcRuntime
        foreach ($name in @('px_rdp_client.dll', 'px_rdp_core.dll', 'px_rdp_winpr.dll', 'libusb-1.0.dll',
                            'libssl-3-x64.dll', 'libcrypto-3-x64.dll', 'zlib1.dll', 'cjson.dll', 'legacy.dll', 'openh264-6.dll')) {
            Publish-VerifiedFile -Source (Join-Path $buildRoot ('src\px_deps\' + $name)) `
                -Destination (Join-Path $distRoot $name) -ProcessName 'px_client'
        }
        # The build may stage GPU compiler and loader runtimes during a clean build.
        foreach ($name in @('d3dcompiler_47.dll', 'dxcompiler.dll', 'dxil.dll', 'vulkan-1.dll')) {
            $source = Join-Path $buildRoot ('src\px_deps\' + $name)
            if (Test-Path -LiteralPath $source -PathType Leaf) {
                Publish-VerifiedFile -Source $source -Destination (Join-Path $distRoot $name) -ProcessName 'px_client'
            }
        }
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot 'src\px_deps\libplacebo-349.dll') `
            -Destination (Join-Path $distRoot 'libplacebo-349.dll') `
            -ProcessName 'px_client'
        Publish-VerifiedFile -Source (Join-Path $buildRoot 'src\px_deps\rdp\px_rdp_sdk.json') `
            -Destination (Join-Path $distRoot 'rdp\px_rdp_sdk.json') -ProcessName 'px_client'
        $retiredRdpLicenses = Join-Path $distRoot 'rdp\licenses'
        if (Test-Path -LiteralPath $retiredRdpLicenses -PathType Container) {
            Remove-Item -LiteralPath $retiredRdpLicenses -Recurse -Force
            Write-Host "REMOVED retired RDP license output: $retiredRdpLicenses"
        }
        # Voice processing is a shared runtime dependency of Client and Render.
        # The client executable above is published first, stopping any active client.
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\px_voice_call\px_voice_apm.dll") `
            -Destination (Join-Path $distRoot "px_voice_apm.dll") `
            -ProcessName "px_render"
        Remove-RetiredClientRecordingCore
        $retiredClientPluginDirectory = Join-Path $distRoot "deps\ct_plugins"
        foreach ($retiredName in @(
            "clipboard.dll", "ft.dll", "record.dll",
            "client_clipboard.dll", "ft_client.dll",
            "media_record_client.dll")) {
            $retiredPath = Join-Path $retiredClientPluginDirectory $retiredName
            if (Test-Path -LiteralPath $retiredPath -PathType Leaf) {
                Remove-Item -LiteralPath $retiredPath -Force
                Write-Host "REMOVED retired Client plug-in $retiredPath"
            }
        }
        if ((Test-Path -LiteralPath $retiredClientPluginDirectory -PathType Container) -and
            -not (Get-ChildItem -LiteralPath $retiredClientPluginDirectory -Force)) {
            Remove-Item -LiteralPath $retiredClientPluginDirectory
        }
        Publish-DesktopBrandLogo -ProcessName "px_client"
        Publish-DesktopPlatformIcons -ProcessName "px_client"
        Publish-LanguageResources
    }
    "panel" {
        Remove-RetiredQtArtifacts
        Remove-RetiredPanelArtifacts
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\px_panel.exe") `
            -Destination (Join-Path $distRoot "px_panel.exe") `
            -ProcessName "px_panel"
        foreach ($name in @('d3dcompiler_47.dll', 'dxcompiler.dll', 'dxil.dll', 'vulkan-1.dll', 'libplacebo-349.dll')) {
            $source = Join-Path $buildRoot ('src\px_deps\' + $name)
            if (Test-Path -LiteralPath $source -PathType Leaf) {
                Publish-VerifiedFile -Source $source -Destination (Join-Path $distRoot $name) -ProcessName 'px_panel'
            }
        }
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\resources\fonts\Roboto-Regular.ttf") `
            -Destination (Join-Path $distRoot "resources\fonts\Roboto-Regular.ttf") `
            -ProcessName "px_panel"
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\resources\licenses\Lucide.txt") `
            -Destination (Join-Path $distRoot "resources\licenses\Lucide.txt") `
            -ProcessName "px_panel"
        Publish-DesktopBrandLogo -ProcessName "px_panel"
        Publish-DesktopPlatformIcons -ProcessName "px_panel"
        Publish-LanguageResources
    }
    "render_network_library" {
        Remove-LegacyRenderPluginDirectory
        Remove-RetiredRenderNetworkLibraries
        if ([string]::IsNullOrWhiteSpace($LibraryTarget)) {
            throw "LibraryTarget is required for render_network_library"
        }
        Publish-RenderNetworkLibrary -Target $LibraryTarget
    }
    "hook_audio" {
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_render\hook_capture\win\hk_obs\layers\pixels-vulkan64.json") `
            -Destination (Join-Path $distRoot "layers\pixels-vulkan64.json") `
            -ProcessName "px_render"
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_render\hook_capture\win\hk_obs\px_gh.dll") `
            -Destination (Join-Path $distRoot "px_gh.dll") `
            -ProcessName "px_render"
    }
    "render_network_libraries" {
        Remove-LegacyRenderPluginDirectory
        Remove-RetiredRenderNetworkLibraries
        foreach ($target in $renderNetworkLibraryMap.Keys | Sort-Object) {
            Publish-RenderNetworkLibrary -Target $target
        }
        Publish-RenderCefRuntime
    }
    "ft_protocol" {
        Remove-RetiredQtArtifacts
        Remove-LegacyRenderPluginDirectory
        Remove-RetiredRenderNetworkLibraries
        # px_file_transfer.proto objects cross these executable/plugin boundaries.
        # Publish them as one compatibility unit so generated protobuf layouts cannot be mixed.
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\px_panel.exe") `
            -Destination (Join-Path $distRoot "px_panel.exe") `
            -ProcessName "px_panel"
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_render\px_render.exe") `
            -Destination (Join-Path $distRoot "px_render.exe") `
            -ProcessName "px_render"
        Publish-RenderNetworkLibrary -Target "net_rtc_local"
        Publish-RenderCefRuntime
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\px_client.exe") `
            -Destination (Join-Path $distRoot "px_client.exe") `
            -ProcessName "px_client"
        Move-RetiredClientRtcRuntime
        Remove-RetiredClientRecordingCore
        $retiredClientPluginDirectory = Join-Path $distRoot "deps\ct_plugins"
        foreach ($retiredName in @(
            "clipboard.dll", "ft.dll", "record.dll",
            "client_clipboard.dll", "ft_client.dll",
            "media_record_client.dll")) {
            $retiredPath = Join-Path $retiredClientPluginDirectory $retiredName
            if (Test-Path -LiteralPath $retiredPath -PathType Leaf) {
                Remove-Item -LiteralPath $retiredPath -Force
                Write-Host "REMOVED retired Client plug-in $retiredPath"
            }
        }
        if ((Test-Path -LiteralPath $retiredClientPluginDirectory -PathType Container) -and
            -not (Get-ChildItem -LiteralPath $retiredClientPluginDirectory -Force)) {
            Remove-Item -LiteralPath $retiredClientPluginDirectory
        }
        Publish-LanguageResources
    }
}
$manifestRefresh = Join-Path $repoRoot 'scripts\refresh_development_dist.py'
& python $manifestRefresh $distRoot
if ($LASTEXITCODE -ne 0) {
    throw "development distribution manifest refresh failed with exit code $LASTEXITCODE"
}
}
finally {
    Restore-RenderServiceAfterPublish
}
