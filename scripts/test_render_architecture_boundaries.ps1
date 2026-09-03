param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
else {
    $RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
}

$architectureRoot = Join-Path $RepoRoot "src\px_render\architecture"
$nativeFiles = Get-ChildItem -LiteralPath $architectureRoot -Recurse -File |
    Where-Object { $_.Extension -in @(".h", ".hpp", ".cpp", ".cc", ".cxx") }
$forbidden = [ordered]@{
    "std::any" = "new architecture data paths must use typed values"
    "PX_PLUGIN_EXPORT" = "built-in modules must not export the legacy plug-in ABI"
    "GetPluginById" = "built-in modules must not use the legacy service locator"
    "AttachPlugin" = "built-in modules must not create all-to-all plug-in routing"
    "PluginManager" = "the new architecture must not depend on PluginManager"
    "NOLINT\(gammaray-raw-pointer-boundary\)" = "new internal code cannot add ABI exceptions"
}

$violations = [System.Collections.Generic.List[string]]::new()
foreach ($file in $nativeFiles) {
    $content = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($entry in $forbidden.GetEnumerator()) {
        if ($content -match $entry.Key) {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $violations.Add("${relative}: $($entry.Value)")
        }
    }
}

# The compatibility service locator and untyped control dispatch are retired
# repository-wide for active Render C++ code. Keeping this scan broader than
# architecture/ prevents an old built-in module from silently recreating the
# all-to-all graph.
$renderNativeFiles = Get-ChildItem -LiteralPath `
    (Join-Path $RepoRoot "src\px_render") -Recurse -File |
    Where-Object { $_.Extension -in @(".h", ".hpp", ".cpp", ".cc", ".cxx") }
foreach ($file in $renderNativeFiles) {
    $content = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($entry in ([ordered]@{
        "GetPluginById" = "UUID service lookup is retired"
        "AttachPlugin" = "all-to-all module attachment is retired"
        "AttachNetPlugin" = "all-to-all network attachment is retired"
        "OnMessageRaw" = "network control must use typed commands"
    }).GetEnumerator()) {
        if ($content -match $entry.Key) {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $violations.Add("${relative}: $($entry.Value)")
        }
    }
}

$architectureCmake = Get-Content -LiteralPath (Join-Path $architectureRoot "CMakeLists.txt") -Raw
if ($architectureCmake -match 'add_library\s*\([^\)]*\b(SHARED|MODULE)\b') {
    $violations.Add("architecture/CMakeLists.txt: architecture core must be statically linked")
}
if ($architectureCmake -match '\bpx_plugin\b') {
    $violations.Add("architecture/CMakeLists.txt: architecture core must not link the legacy plug-in framework")
}

$moduleRegistry = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\modules\render_module_registry.h") -Raw
foreach ($pattern in @("PluginManager", "GetPluginById", "std::map")) {
    if ($moduleRegistry -match $pattern) {
        $violations.Add("render_module_registry.h: explicit Render module composition regressed to $pattern")
    }
}

$eventIngress = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\ingress\render_event_ingress.cpp") -Raw
foreach ($eventType in @(
    "kPluginEncodedVideoFrameEvent",
    "kPluginCapturedVideoFrameEvent",
    "kPluginCursorEvent")) {
    if ($eventIngress -match $eventType) {
        $violations.Add("render_event_ingress.cpp: high-frequency $eventType returned to generic routing")
    }
}

$typedVideoFiles = @(
    "src\px_render\plugin_interface\px_video_encoder_plugin.h",
    "src\px_render\plugins\ffmpeg_encoder\ffmpeg_encoder.h",
    "src\px_render\plugins\ffmpeg_encoder\ffmpeg_encoder.cpp",
    "src\px_render\plugins\amf_encoder\video_encoder_vce.h",
    "src\px_render\plugins\amf_encoder\video_encoder_vce.cpp",
    "src\px_render\plugins\nvenc_encoder\nvenc_video_encoder.h",
    "src\px_render\plugins\nvenc_encoder\nvenc_video_encoder.cpp",
    "src\px_render\pipeline\encoded_video_fanout.cpp"
)
foreach ($relativePath in $typedVideoFiles) {
    $content = Get-Content -LiteralPath (Join-Path $RepoRoot $relativePath) -Raw
    if ($content -match "std::any|any_cast") {
        $violations.Add("${relativePath}: video data path must use typed CaptureVideoFrame metadata")
    }
}

$webRtcHost = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\network\webrtc_library_host.cpp") -Raw
if ($webRtcHost -match "directory_iterator") {
    $violations.Add("webrtc_library_host.cpp: fixed WebRTC loading must not scan directories")
}
foreach ($libraryName in @("net_rtc", "net_rtc_local")) {
    if ($webRtcHost -notmatch [regex]::Escape($libraryName)) {
        $violations.Add("webrtc_library_host.cpp: missing explicit $libraryName boundary")
    }
}
$webRtcHostHeader = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\network\webrtc_library_host.h") -Raw
if ($webRtcHostHeader -match "PxPluginInterface") {
    $violations.Add("webrtc_library_host.h: fixed WebRTC host must expose a network component, not the generic plug-in interface")
}

$wsBuiltInFiles = Get-ChildItem -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\plugins\net_ws") -Recurse -File |
    Where-Object { $_.Extension -in @(".h", ".hpp", ".cpp", ".cc", ".cxx") }
foreach ($file in $wsBuiltInFiles) {
    if ((Get-Content -LiteralPath $file.FullName -Raw) -match "GetPluginById") {
        $violations.Add("$($file.FullName): built-in WS transport cannot use the legacy service locator")
    }
}

if ($violations.Count -gt 0) {
    throw "Render architecture boundary guard failed:`n$($violations -join "`n")"
}

Write-Host "PASS: new Render architecture code is static, typed, and independent of the legacy plug-in framework."
