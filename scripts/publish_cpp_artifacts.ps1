param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("render", "client", "panel", "render_plugins", "render_plugin")]
    [string]$Component,
    [string]$PluginTarget = "",
    [string]$BuildDir = "build_official"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = Join-Path $repoRoot $BuildDir
$distRoot = Join-Path $buildRoot "dist"

if (-not (Test-Path -LiteralPath $distRoot -PathType Container)) {
    throw "dist directory does not exist: $distRoot"
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
        $running = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
        if (-not $running) {
            throw
        }
        Write-Host "Stopping $ProcessName because its runtime artifact is in use."
        $running | Stop-Process -Force
        $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
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

$renderPluginMap = @{
    "enc_amf"          = "amf_encoder\enc_amf.dll"
    "clipboard"        = "clipboard\clipboard.dll"
    "cap_dda"          = "dda_capture\cap_dda.dll"
    "event_replayer"   = "event_replayer\event_replayer.dll"
    "enc_ffmpeg"       = "ffmpeg_encoder\enc_ffmpeg.dll"
    "frame_carrier"    = "frame_carrier\frame_carrier.dll"
    "frame_debugger"   = "frame_debugger\frame_debugger.dll"
    "frame_resizer"    = "frame_resizer\frame_resizer.dll"
    "ft"               = "ft\ft.dll"
    "cap_gdi"          = "gdi_capture\cap_gdi.dll"
    "joystick"         = "joystick\joystick.dll"
    "live_pusher"      = "live_pusher\live_pusher.dll"
    "media_recorder"   = "media_recorder\media_recorder.dll"
    "mock_video_stream"= "mock_video_stream\mock_video_stream.dll"
    "net_relay"        = "net_relay\net_relay.dll"
    "net_rtc"          = "net_rtc\net_rtc.dll"
    "net_rtc_local"    = "net_rtc_local\net_rtc_local.dll"
    "net_udp"          = "net_udp\net_udp.dll"
    "net_ws"           = "net_ws\net_ws.dll"
    "enc_nvenc"        = "nvenc_encoder\enc_nvenc.dll"
    "obj_detector"     = "obj_detector\obj_detector.dll"
    "enc_opus"         = "opus_encoder\enc_opus.dll"
    "voice_call"       = "voice_call\voice_call.dll"
    "cap_was_audio"    = "was_audio_capture\cap_was_audio.dll"
}

function Publish-RenderPlugin {
    param([Parameter(Mandatory = $true)][string]$Target)
    if (-not $renderPluginMap.ContainsKey($Target)) {
        throw "unknown Render plugin target: $Target"
    }
    $relativeSource = $renderPluginMap[$Target]
    $source = Join-Path $buildRoot ("src\px_render\plugins\" + $relativeSource)
    $destination = Join-Path $distRoot ("deps\rd_plugins\" + (Split-Path -Leaf $relativeSource))
    Publish-VerifiedFile -Source $source -Destination $destination -ProcessName "px_render"
}

switch ($Component) {
    "render" {
        Publish-VerifiedFile `
            -Source (Join-Path $buildRoot "src\px_render\px_render.exe") `
            -Destination (Join-Path $distRoot "px_render.exe") `
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
        $clientPlugins = @(
            @("clipboard\clipboard.dll", "clipboard.dll"),
            @("ft\ft.dll", "ft.dll"),
            @("media_record\record.dll", "record.dll")
        )
        foreach ($plugin in $clientPlugins) {
            Publish-VerifiedFile `
                -Source (Join-Path $buildRoot ("src\px_client\plugins\" + $plugin[0])) `
                -Destination (Join-Path $distRoot ("deps\ct_plugins\" + $plugin[1])) `
                -ProcessName "px_client"
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
    "render_plugin" {
        if ([string]::IsNullOrWhiteSpace($PluginTarget)) {
            throw "PluginTarget is required for render_plugin"
        }
        Publish-RenderPlugin -Target $PluginTarget
    }
    "render_plugins" {
        foreach ($target in $renderPluginMap.Keys | Sort-Object) {
            Publish-RenderPlugin -Target $target
        }
    }
}
