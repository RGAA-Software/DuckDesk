param(
    [string]$RepoRoot = "",
    [switch]$CheckDist
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
else {
    $RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
}

function Assert-NotMatch {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Reason
    )
    $content = Get-Content -LiteralPath $Path -Raw
    if ($content -match $Pattern) {
        throw "$Reason ($Path)"
    }
}

function Assert-Match {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Reason
    )
    $content = Get-Content -LiteralPath $Path -Raw
    if ($content -notmatch $Pattern) {
        throw "$Reason ($Path)"
    }
}

$pluginCmake = Join-Path $RepoRoot "src\px_render\plugins\CMakeLists.txt"
$rootCmake = Join-Path $RepoRoot "CMakeLists.txt"
$publisher = Join-Path $RepoRoot "scripts\publish_cpp_artifacts.ps1"
$collector = Join-Path $RepoRoot "scripts\collect_dist.py"

foreach ($module in @("mock_video_stream", "obj_detector")) {
    Assert-NotMatch `
        -Path $pluginCmake `
        -Pattern "add_subdirectory\s*\(\s*$module\s*\)" `
        -Reason "$module is still in the Render production CMake graph"
    Assert-NotMatch `
        -Path $rootCmake `
        -Pattern "(?m)^\s*[^#\r\n]*\b$module\b" `
        -Reason "$module is still in a root production target"
    Assert-NotMatch `
        -Path $publisher `
        -Pattern ('(?m)^\s*"{0}"\s*=' -f [regex]::Escape($module)) `
        -Reason "$module can still be published as a Render plugin target"
}

$runtimeFiles = @(
    "src\px_render\rd_app.h",
    "src\px_render\rd_app.cpp",
    "src\px_render\rd_main.cpp",
    "src\px_render\settings\rd_settings.h",
    "src\px_render\app\encoder_thread.cpp",
    "src\px_render\modules\render_module_registry.h",
    "src\px_render\modules\render_module_registry.cpp",
    "src\px_panel\src\render_panel\px_settings.h",
    "src\px_panel\src\render_panel\px_render_controller.cpp"
)
foreach ($relativePath in $runtimeFiles) {
    Assert-NotMatch `
        -Path (Join-Path $RepoRoot $relativePath) `
        -Pattern '(?i)mock_video|GetMockVideoStreamPlugin' `
        -Reason "retired mock video runtime path is still reachable"
}

foreach ($dll in @("mock_video_stream.dll", "obj_detector.dll")) {
    $escapedDll = [regex]::Escape($dll)
    Assert-Match `
        -Path $publisher `
        -Pattern $escapedDll `
        -Reason "$dll is missing from focused-publish stale cleanup"
    Assert-Match `
        -Path $collector `
        -Pattern $escapedDll `
        -Reason "$dll is missing from full-dist exclusion"
}

# frame_debugger is retained as a built-in Observer, so its legacy DLL must
# follow the same production exclusion rules without being classified as dead.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*frame_debugger\s*\)" `
    -Reason "frame_debugger is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\bframe_debugger\b" `
    -Reason "frame_debugger is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"frame_debugger"\s*=' `
    -Reason "frame_debugger can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("frame_debugger.dll")) `
        -Reason "frame_debugger.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "observers/frame_debugger_observer\.cpp" `
    -Reason "the built-in frame debugger Observer is not statically compiled"

# media_recorder is retained as a built-in Sink. Its stable module UUID and
# Panel UX remain, while the generic stream plug-in ABI and DLL are retired.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*media_recorder\s*\)" `
    -Reason "media_recorder is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\bmedia_recorder\b" `
    -Reason "media_recorder is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"media_recorder"\s*=' `
    -Reason "media_recorder can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("media_recorder.dll")) `
        -Reason "media_recorder.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "sinks/media_recorder_sink\.cpp" `
    -Reason "the built-in media recorder Sink is not statically compiled"

# live_pusher is retained as a built-in Sink and uses the typed media bus.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*live_pusher\s*\)" `
    -Reason "live_pusher is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\blive_pusher\b" `
    -Reason "live_pusher is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"live_pusher"\s*=' `
    -Reason "live_pusher can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("live_pusher.dll")) `
        -Reason "live_pusher.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "sinks/live_pusher_sink\.cpp" `
    -Reason "the built-in live pusher Sink is not statically compiled"

# frame_resizer is retained as a built-in GPU Processor.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*frame_resizer\s*\)" `
    -Reason "frame_resizer is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\bframe_resizer\b" `
    -Reason "frame_resizer is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"frame_resizer"\s*=' `
    -Reason "frame_resizer can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("frame_resizer.dll")) `
        -Reason "frame_resizer.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "processors/frame_resizer_processor\.cpp" `
    -Reason "the built-in frame resizer Processor is not statically compiled"

# frame_carrier is retained as a built-in shared-texture Processor.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*frame_carrier\s*\)" `
    -Reason "frame_carrier is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\bframe_carrier\b" `
    -Reason "frame_carrier is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"frame_carrier"\s*=' `
    -Reason "frame_carrier can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("frame_carrier.dll")) `
        -Reason "frame_carrier.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "processors/frame_carrier_processor\.cpp" `
    -Reason "the built-in frame carrier Processor is not statically compiled"

# enc_opus is retained as a built-in audio Processor.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*opus_encoder\s*\)" `
    -Reason "enc_opus is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\benc_opus\b" `
    -Reason "enc_opus is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"enc_opus"\s*=' `
    -Reason "enc_opus can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("enc_opus.dll")) `
        -Reason "enc_opus.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "processors/opus_encoder_processor\.cpp" `
    -Reason "the built-in Opus Processor is not statically compiled"

# event_replayer is retained as a built-in input Service.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*event_replayer\s*\)" `
    -Reason "event_replayer is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\bevent_replayer\b" `
    -Reason "event_replayer is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"event_replayer"\s*=' `
    -Reason "event_replayer can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("event_replayer.dll")) `
        -Reason "event_replayer.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "services/input_replay_service\.cpp" `
    -Reason "the built-in input replay Service is not statically compiled"

# cap_was_audio is retained as a built-in audio Source. Its low-level WASAPI
# implementations stay in place, but the generic data-provider DLL is retired.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*was_audio_capture\s*\)" `
    -Reason "cap_was_audio is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\bcap_was_audio\b" `
    -Reason "cap_was_audio is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"cap_was_audio"\s*=' `
    -Reason "cap_was_audio can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("cap_was_audio.dll")) `
        -Reason "cap_was_audio.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "sources/was_audio_capture_source\.cpp" `
    -Reason "the built-in WAS audio Source is not statically compiled"

# Clipboard traffic is already handled directly by the typed UserProxy/network
# path. The inactive generic utility plug-in boundary must not return.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*clipboard\s*\)" `
    -Reason "clipboard is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\bclipboard\b" `
    -Reason "clipboard is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"clipboard"\s*=' `
    -Reason "clipboard can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("clipboard.dll")) `
        -Reason "clipboard.dll is missing from stale artifact exclusion"
}

# joystick is retained as a built-in Service with direct typed network ingress.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*joystick\s*\)" `
    -Reason "joystick is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\bjoystick\b" `
    -Reason "joystick is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"joystick"\s*=' `
    -Reason "joystick can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("joystick.dll")) `
        -Reason "joystick.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "services/joystick_service\.cpp" `
    -Reason "the built-in joystick Service is not statically compiled"

# File transfer is retained as a built-in routed Service. Network transports
# remain responsible only for carrying its typed protocol messages.
Assert-NotMatch `
    -Path $pluginCmake `
    -Pattern "add_subdirectory\s*\(\s*ft\s*\)" `
    -Reason "ft is still in the Render DLL CMake graph"
Assert-NotMatch `
    -Path $rootCmake `
    -Pattern "(?m)^\s*[^#\r\n]*\bft\b" `
    -Reason "ft is still in a root production DLL target"
Assert-NotMatch `
    -Path $publisher `
    -Pattern '(?m)^\s*"ft"\s*=' `
    -Reason "ft can still be published as a Render DLL"
foreach ($path in @($publisher, $collector)) {
    Assert-Match `
        -Path $path `
        -Pattern ([regex]::Escape("ft.dll")) `
        -Reason "ft.dll is missing from stale artifact exclusion"
}
Assert-Match `
    -Path (Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt") `
    -Pattern "plugins/ft/ft_plugin\.cpp" `
    -Reason "the built-in file-transfer Service is not statically compiled"

if ($CheckDist) {
    $distPluginDirectory = Join-Path $RepoRoot "build_official\dist\deps\rd_plugins"
    foreach ($dll in @(
        "mock_video_stream.dll",
        "obj_detector.dll",
        "frame_debugger.dll",
        "media_recorder.dll",
        "live_pusher.dll",
        "frame_resizer.dll",
        "frame_carrier.dll",
        "enc_opus.dll",
        "event_replayer.dll",
        "cap_was_audio.dll",
        "clipboard.dll",
        "joystick.dll",
        "ft.dll",
        "voice_call.dll",
        "cap_dda.dll",
        "cap_gdi.dll",
        "enc_ffmpeg.dll",
        "enc_amf.dll",
        "enc_nvenc.dll",
        "net_ws.dll",
        "net_udp.dll",
        "net_relay.dll")) {
        $retiredArtifact = Join-Path $distPluginDirectory $dll
        if (Test-Path -LiteralPath $retiredArtifact -PathType Leaf) {
            throw "retired Render artifact is still present: $retiredArtifact"
        }
    }
}

Write-Host "PASS: retired Render modules are excluded from production build and publication."
if ($CheckDist) {
    Write-Host "PASS: retired Render module DLLs are absent from build_official\dist."
}
