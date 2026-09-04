param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("render", "client", "panel", "render_network_libraries", "render_network_library", "hook_audio", "ft_protocol")]
    [string]$Component,
    [string]$LibraryTarget = "",
    [string]$BuildDir = "build_official"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = Join-Path $repoRoot $BuildDir
$distRoot = Join-Path $buildRoot "dist"
$restartRenderService = $false

if (-not (Test-Path -LiteralPath $distRoot -PathType Container)) {
    throw "dist directory does not exist: $distRoot"
}

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

    $sourceHash = Get-Sha256Hex -Path $Source
    $destinationHash = Get-Sha256Hex -Path $Destination
    if ($sourceHash -ne $destinationHash) {
        throw "SHA-256 mismatch after publish: $Destination"
    }
    $relative = $Destination.Substring($distRoot.Length).TrimStart([char]'\')
    Write-Host "HASH OK  $relative  $destinationHash"
}

function Publish-LanguageResources {
    $sourceRoot = Join-Path $repoRoot "src\px_panel\resources\language"
    $destinationRoot = Join-Path $distRoot "resources\language"
    Get-ChildItem -LiteralPath $sourceRoot -File -Recurse | ForEach-Object {
        $relative = $_.FullName.Substring($sourceRoot.Length).TrimStart([char]'\')
        Publish-VerifiedFile -Source $_.FullName -Destination (Join-Path $destinationRoot $relative)
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

$renderNetworkLibraryMap = @{
    "net_rtc"       = "network\webrtc\remote\net_rtc.dll"
    "net_rtc_local" = "network\webrtc\local\net_rtc_local.dll"
}

function Publish-RenderNetworkLibrary {
    param([Parameter(Mandatory = $true)][string]$Target)
    if (-not $renderNetworkLibraryMap.ContainsKey($Target)) {
        throw "unknown Render network library target: $Target"
    }
    $relativeSource = $renderNetworkLibraryMap[$Target]
    $source = Join-Path $buildRoot ("src\px_render\" + $relativeSource)
    $destination = Join-Path $distRoot ("deps\network\" + (Split-Path -Leaf $relativeSource))
    Publish-VerifiedFile -Source $source -Destination $destination -ProcessName "px_render"
}

try {
switch ($Component) {
    "render" {
        Remove-LegacyRenderPluginDirectory
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_render\px_render.exe") `
            -Destination (Join-Path $distRoot "px_render.exe") `
            -ProcessName "px_render"
        Publish-VerifiedFile `
            -Source (Join-Path $repoRoot "src\px_render\architecture\processors\frame_carrier\resources\ic_logo_point.png") `
            -Destination (Join-Path $distRoot "resources\render\frame_carrier\ic_logo_point.png") `
            -ProcessName "px_render"
    }
    "client" {
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_client\px_client.exe") `
            -Destination (Join-Path $distRoot "px_client.exe") `
            -ProcessName "px_client"
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\px_webrtc_client\px_rtc_client.dll") `
            -Destination (Join-Path $distRoot "px_client_rtc.dll") `
            -ProcessName "px_client"
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
    "panel" {
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\px_panel.exe") `
            -Destination (Join-Path $distRoot "px_panel.exe") `
            -ProcessName "px_panel"
        Get-ChildItem -LiteralPath (Join-Path $buildRoot "src\px_deps\px_skins") -File | `
            Where-Object { $_.Name -eq "skin_config.toml" -or $_.Name -like "skin_*.dll" } | `
            ForEach-Object {
                Publish-VerifiedFile `
                    -Source $_.FullName `
                    -Destination (Join-Path $distRoot ("deps\theme\" + $_.Name)) `
                    -ProcessName "px_panel"
            }
        Publish-LanguageResources
    }
    "render_network_library" {
        Remove-LegacyRenderPluginDirectory
        if ([string]::IsNullOrWhiteSpace($LibraryTarget)) {
            throw "LibraryTarget is required for render_network_library"
        }
        Publish-RenderNetworkLibrary -Target $LibraryTarget
    }
    "hook_audio" {
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_render\hook_capture\win\hk_obs\px_gh.dll") `
            -Destination (Join-Path $distRoot "px_gh.dll") `
            -ProcessName "px_render"
    }
    "render_network_libraries" {
        Remove-LegacyRenderPluginDirectory
        foreach ($target in $renderNetworkLibraryMap.Keys | Sort-Object) {
            Publish-RenderNetworkLibrary -Target $target
        }
    }
    "ft_protocol" {
        Remove-LegacyRenderPluginDirectory
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
        foreach ($target in @("net_rtc", "net_rtc_local")) {
            Publish-RenderNetworkLibrary -Target $target
        }
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_client\px_client.exe") `
            -Destination (Join-Path $distRoot "px_client.exe") `
            -ProcessName "px_client"
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_deps\px_webrtc_client\px_rtc_client.dll") `
            -Destination (Join-Path $distRoot "px_client_rtc.dll") `
            -ProcessName "px_client"
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
}
finally {
    Restore-RenderServiceAfterPublish
}
