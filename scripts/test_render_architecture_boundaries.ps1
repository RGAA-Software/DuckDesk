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
# The strict typed/logging gate covers architecture-native orchestration and
# wrappers. Retained low-level implementations were physically moved beneath
# role-specific subdirectories in stage 17; they keep their dedicated focused
# tests and ownership gate and are not reclassified as newly authored code by
# a path-only move.
$retainedImplementationPattern = [regex]::new(
    'architecture\\(?:encoders\\(?:amf|ffmpeg|nvenc|opus)|processors\\(?:frame_carrier|frame_resizer)|services\\(?:event_replayer|joystick|voice_call)|sinks\\live_pusher|sources\\(?:dda|gdi|was_audio))\\')
$nativeFiles = Get-ChildItem -LiteralPath $architectureRoot -Recurse -File |
    Where-Object {
        $_.Extension -in @(".h", ".hpp", ".cpp", ".cc", ".cxx") -and
        -not $retainedImplementationPattern.IsMatch($_.FullName)
    }
$forbidden = [ordered]@{
    "std::any" = "new architecture data paths must use typed values"
    "PX_PLUGIN_EXPORT" = "built-in modules must not export the legacy plug-in ABI"
    "GetPluginById" = "built-in modules must not use the legacy service locator"
    "AttachPlugin" = "built-in modules must not create all-to-all plug-in routing"
    "PluginManager" = "the new architecture must not depend on PluginManager"
}

$violations = [System.Collections.Generic.List[string]]::new()

# Stage 18 makes the built-in roles ordinary statically linked modules. Keep
# both the new names and the absence of their legacy plug-in wrappers under
# source-control enforcement so a later feature cannot restore the old shape.
$requiredModuleFiles = @(
    "src\px_render\architecture\modules\render_module.h",
    "src\px_render\architecture\encoders\video_encoder_module.h",
    "src\px_render\architecture\sources\monitor_capture_source.h",
    "src\px_render\architecture\extensions\flow_node_plugin.h",
    "src\px_render\network\transport_types.h",
    "src\px_render\architecture\sources\dda\dda_capture_source.h",
    "src\px_render\architecture\sources\gdi\gdi_capture_source.h",
    "src\px_render\architecture\encoders\ffmpeg\ffmpeg_video_encoder.h",
    "src\px_render\architecture\encoders\amf\amf_video_encoder.h",
    "src\px_render\architecture\encoders\nvenc\nvenc_encoder_module.h",
    "src\px_render\network\ws\ws_transport.h",
    "src\px_render\network\udp\udp_transport.h",
    "src\px_render\network\relay\relay_transport.h"
)
foreach ($relativePath in $requiredModuleFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $relativePath))) {
        $violations.Add("${relativePath}: required built-in module contract is missing")
    }
}

$retiredPluginFiles = @(
    "src\px_render\architecture\sources\dda\dda_capture_plugin.h",
    "src\px_render\architecture\sources\gdi\gdi_capture_plugin.h",
    "src\px_render\architecture\encoders\ffmpeg\ffmpeg_encoder_plugin.h",
    "src\px_render\architecture\encoders\amf\amf_encoder_plugin.h",
    "src\px_render\architecture\encoders\nvenc\nvenc_encoder_plugin.h",
    "src\px_render\network\ws\ws_plugin.h",
    "src\px_render\network\udp\udp_plugin.h",
    "src\px_render\network\relay\relay_plugin.h",
    "src\px_render\plugin_interface\px_audio_encoder_plugin.h",
    "src\px_render\plugin_interface\px_data_provider_plugin.h",
    "src\px_render\plugin_interface\px_frame_carrier_plugin.h",
    "src\px_render\plugin_interface\px_frame_processor_plugin.h",
    "src\px_render\plugin_interface\px_monitor_capture_plugin.h",
    "src\px_render\plugin_interface\px_stream_plugin.h",
    "src\px_render\plugin_interface\px_video_encoder_plugin.h"
)
foreach ($relativePath in $retiredPluginFiles) {
    if (Test-Path -LiteralPath (Join-Path $RepoRoot $relativePath)) {
        $violations.Add("${relativePath}: retired built-in plug-in wrapper returned")
    }
}

$builtInModuleHeaders = $requiredModuleFiles |
    Where-Object { $_ -notmatch 'flow_node_plugin|transport_types' }
foreach ($relativePath in $builtInModuleHeaders) {
    $fullPath = Join-Path $RepoRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath)) {
        continue
    }
    $content = Get-Content -LiteralPath $fullPath -Raw
    if ($content -match ':\s*public\s+(?:PxPluginInterface|PxNetPlugin|PxVideoEncoderPlugin|PxMonitorCapturePlugin|PxStreamPlugin)') {
        $violations.Add("${relativePath}: built-in module must not inherit a legacy plug-in interface")
    }
}

$flowNodeContractPath = Join-Path $RepoRoot `
    "src\px_render\architecture\extensions\flow_node_plugin.h"
if (Test-Path -LiteralPath $flowNodeContractPath) {
    $flowNodeContract = Get-Content -LiteralPath $flowNodeContractPath -Raw
    foreach ($role in @(
        "VideoSourcePlugin", "AudioSourcePlugin", "VideoProcessorPlugin",
        "AudioProcessorPlugin", "VideoEncoderPlugin", "AudioEncoderPlugin",
        "ObserverPlugin", "SinkPlugin")) {
        if ($flowNodeContract -notmatch ("class\s+" + $role + "\b")) {
            $violations.Add("flow_node_plugin.h: missing pipeline role $role")
        }
    }
    foreach ($forbiddenFlowSymbol in @("PxPluginInterface", "PxNetPlugin", "GetInstance")) {
        if ($flowNodeContract -match $forbiddenFlowSymbol) {
            $violations.Add("flow_node_plugin.h: extension contract exposes legacy symbol $forbiddenFlowSymbol")
        }
    }
}
foreach ($file in $nativeFiles) {
    $content = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($entry in $forbidden.GetEnumerator()) {
        if ($content -match $entry.Key) {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $violations.Add("${relative}: $($entry.Value)")
        }
    }
    foreach ($match in [regex]::Matches(
        $content, '(?s)LOG[EW]\s*\((.*?)\);')) {
        $statement = $match.Groups[1].Value
        $missingFields = @(
            "event", "component", "code", "operation", "outcome", "recoverable"
        ) | Where-Object { $statement -notmatch ("\b" + $_ + "=") }
        if ($missingFields.Count -gt 0) {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $line = 1 + ([regex]::Matches(
                $content.Substring(0, $match.Index), "`n")).Count
            $violations.Add(
                "${relative}:${line}: WARN/ERROR missing fields $($missingFields -join ',')")
        }
        if ($statement -match '\bmonitor=\{\}' -and
            $statement -notmatch 'PrivacyLogId') {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $violations.Add("${relative}: monitor identifiers in logs must use PrivacyLogId")
        }
    }
    foreach ($match in [regex]::Matches(
        $content, '(?s)LOG[IEW]\s*\((.*?)\);')) {
        $statement = $match.Groups[1].Value
        if ($statement -match '\bmonitor=\{\}' -and
            $statement -notmatch 'PrivacyLogId') {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $violations.Add("${relative}: monitor identifiers in logs must use PrivacyLogId")
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
foreach ($pattern in @(
    "PluginManager",
    "GetPluginById",
    "GetRtcTransport",
    "GetRtcLocalTransport",
    "GetUdpTransport",
    "GetRelayTransport",
    "std::map",
    "PxVideoEncoderPlugin",
    "PxMonitorCapturePlugin",
    "vector\s*<\s*std::shared_ptr\s*<\s*PxNetPlugin")) {
    if ($moduleRegistry -match $pattern) {
        $violations.Add("render_module_registry.h: explicit Render module composition regressed to $pattern")
    }
}

$registryPaths = @(
    (Join-Path $RepoRoot "src\px_render\modules\render_module_registry.cpp"),
    (Join-Path $RepoRoot "src\px_render\modules\render_module_registry.h"))
foreach ($file in $renderNativeFiles) {
    if ($registryPaths -contains $file.FullName) {
        continue
    }
    $content = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($visitorName in @(
        "VisitAllModules",
        "VisitEncoders",
        "VisitNetworkTransports")) {
        if ($content -match $visitorName) {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $violations.Add("${relative}: generic $visitorName must remain private to explicit composition")
        }
    }
}

$eventIngress = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\ingress\render_event_ingress.cpp") -Raw
foreach ($eventType in @(
    "kPluginEncodedVideoFrameEvent",
    "kPluginCapturedVideoFrameEvent",
    "kPluginCursorEvent",
    "kPluginRawVideoFrameEvent",
    "kPluginRawAudioFrameEvent",
    "kPluginSplitRawAudioFrameEvent",
    "kPluginEncodedAudioFrameEvent")) {
    if ($eventIngress -match $eventType) {
        $violations.Add("render_event_ingress.cpp: high-frequency $eventType returned to generic routing")
    }
}

$typedVideoFiles = @(
    "src\px_render\architecture\encoders\video_encoder_module.h",
    "src\px_render\architecture\encoders\ffmpeg\ffmpeg_encoder.h",
    "src\px_render\architecture\encoders\ffmpeg\ffmpeg_encoder.cpp",
    "src\px_render\architecture\encoders\amf\video_encoder_vce.h",
    "src\px_render\architecture\encoders\amf\video_encoder_vce.cpp",
    "src\px_render\architecture\encoders\nvenc\nvenc_video_encoder.h",
    "src\px_render\architecture\encoders\nvenc\nvenc_video_encoder.cpp",
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
if ($webRtcHostHeader -match "PxPluginInterface|PxNetPlugin") {
    $violations.Add("webrtc_library_host.h: public WebRTC facade must not expose a plug-in interface")
}
if ($webRtcHostHeader -match
    "vector\s*<\s*std::shared_ptr\s*<\s*PxNetPlugin") {
    $violations.Add("webrtc_library_host.h: public WebRTC loading must return concrete library leases")
}
if ($webRtcHostHeader -notmatch "class\s+WebRtcLibrary\s+final") {
    $violations.Add("webrtc_library_host.h: missing concrete WebRTC network library facade")
}
if ($webRtcHost -match "CompatibilityModule") {
    $violations.Add("webrtc_library_host.cpp: compatibility plug-in object must not escape the typed facade")
}
$registryHeader = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\modules\render_module_registry.h") -Raw
$registrySource = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\modules\render_module_registry.cpp") -Raw
if ($registryHeader -match "shared_ptr\s*<\s*PxNetPlugin\s*>\s+rtc(_local)?_transport_") {
    $violations.Add("render_module_registry.h: WebRTC ownership must use the concrete library facade")
}
if ($registrySource -match "(lifecycle_modules_|network_transports_)\.push_back\s*\(\s*(module|library)\s*\)") {
    $violations.Add("render_module_registry.cpp: dynamic WebRTC libraries must not enter generic plug-in collections")
}

$wsBuiltInFiles = Get-ChildItem -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\network\ws") -Recurse -File |
    Where-Object { $_.Extension -in @(".h", ".hpp", ".cpp", ".cc", ".cxx") }
foreach ($file in $wsBuiltInFiles) {
    $content = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($entry in ([ordered]@{
        "GetPluginById" = "built-in WS transport cannot use the legacy service locator"
        "ConfigureNetworkPeers" = "built-in WS transport must inject explicit network capabilities"
        "GetNetworkPeers" = "built-in WS transport cannot traverse the transport graph"
        "GetLocalRtcPlugin" = "built-in WS transport cannot expose RTC plug-in instances"
        "GetUdpTransport" = "built-in WS transport cannot expose UDP plug-in instances"
        "network_peers_" = "built-in WS transport cannot retain a peer graph"
        "condition_variable" = "WS control callbacks must use typed awaitables"
        "\.wait_for\s*\(" = "WS request handlers must not block network callbacks"
        "std::any|any_cast" = "WS router context must remain strongly typed"
        "WsTransport\s*\*" = "built-in WS components must observe the module through weak ownership"
        "Get\s*<\s*WsTransport" = "WS routers must not hide module ownership in a service bag"
        "px_net_plugin\.h" = "built-in WS transport must not depend on the WebRTC compatibility base"
    }).GetEnumerator()) {
        if ($content -match $entry.Key) {
            $violations.Add("$($file.FullName): $($entry.Value)")
        }
    }
    $logContent = [regex]::Replace($content, '(?s)/\*.*?\*/', '')
    $logContent = [regex]::Replace($logContent, '(?m)^\s*//.*$', '')
    foreach ($match in [regex]::Matches(
        $logContent, '(?s)LOG[EW]\s*\((.*?)\);')) {
        $statement = $match.Groups[1].Value
        $missingFields = @(
            "event", "component", "code", "operation", "outcome", "recoverable"
        ) | Where-Object { $statement -notmatch ("\b" + $_ + "=") }
        if ($missingFields.Count -gt 0) {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $line = 1 + ([regex]::Matches(
                $logContent.Substring(0, $match.Index), "`n")).Count
            $violations.Add(
                "${relative}:${line}: WARN/ERROR missing fields $($missingFields -join ',')")
        }
        if ($statement -match '\b(?:stream|device|peer)=\{\}' -and
            $statement -notmatch 'PrivacyLogId') {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $violations.Add("${relative}: session identifiers in logs must use PrivacyLogId")
        }
    }
    foreach ($match in [regex]::Matches(
        $logContent, '(?s)LOG[IEW]\s*\((.*?)\);')) {
        $statement = $match.Groups[1].Value
        if ($statement -match '\b(?:stream|device|peer)=\{\}' -and
            $statement -notmatch 'PrivacyLogId') {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $violations.Add("${relative}: session identifiers in logs must use PrivacyLogId")
        }
    }
}

foreach ($transportDirectory in @("udp", "relay")) {
    $transportFiles = Get-ChildItem -LiteralPath `
        (Join-Path $RepoRoot "src\px_render\network\$transportDirectory") `
        -Recurse -File |
        Where-Object { $_.Extension -in @(".h", ".hpp", ".cpp", ".cc", ".cxx") }
    foreach ($file in $transportFiles) {
        $content = Get-Content -LiteralPath $file.FullName -Raw
        if ($content -match 'px_net_plugin\.h|:\s*public\s+PxNetPlugin') {
            $relative = $file.FullName.Substring($RepoRoot.Length).TrimStart([char]'\')
            $violations.Add("${relative}: built-in transport depends on the WebRTC compatibility base")
        }
    }
}

$coroutineOwnedClientFiles = @(
    "src\px_render\network\render_service_client.cpp",
    "src\px_render\network\ws_panel_client.cpp",
    "src\px_render\hook_capture\win\hk_obs\ws_ipc_client.cpp"
)
foreach ($relativePath in $coroutineOwnedClientFiles) {
    $content = Get-Content -LiteralPath (Join-Path $RepoRoot $relativePath) -Raw
    if ($content -match 'set_auto_reconnect\s*\(\s*true') {
        $violations.Add("${relativePath}: reconnect scheduling must remain owned by the connection coroutine")
    }
    foreach ($required in @("RunConnectionLoop", "PxReconnectBackoff", "WaitUntilDisconnected")) {
        if ($content -notmatch [regex]::Escape($required)) {
            $violations.Add("${relativePath}: coroutine-owned reconnect contract is missing $required")
        }
    }
    $adapterStop = $content.IndexOf("client->post([client]")
    $scopeStop = $content.IndexOf("async_scope_->BeginStop")
    if ($adapterStop -lt 0 -or $scopeStop -lt 0 -or $adapterStop -gt $scopeStop) {
        $violations.Add("${relativePath}: adapter stop must be requested before waiting for coroutine scope drain")
    }
}

$wsServerSource = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\network\ws\ws_server.cpp") -Raw
$wsServerHeader = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\network\ws\ws_server.h") -Raw
if ($wsServerSource -match 'PxAsyncRuntime::Create') {
    $violations.Add("net_ws/ws_server.cpp: WS server must use the composition-root async runtime")
}

$udpTransportSource = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\network\udp\udp_transport.cpp") -Raw
if ($udpTransportSource -match 'module_context_->StartTimer') {
    $violations.Add("net_udp/udp_transport.cpp: UDP control timers must remain owned by its async scope")
}
foreach ($required in @("PxAsyncScope::Create", "WaitForAsyncDelay", "RunHeartbeatSweepLoop", "RunFecWindowLoop")) {
    if ($udpTransportSource -notmatch [regex]::Escape($required)) {
        $violations.Add("net_udp/udp_transport.cpp: coroutine-owned control workflow is missing $required")
    }
}
foreach ($required in @(
    "TransportPerformanceWindow",
    "ObserveInbound",
    "ObserveOutbound",
    "ObserveDropped",
    "SnapshotAndReset",
    "event=transport.window"
)) {
    if ($wsServerSource -notmatch [regex]::Escape($required) -and
        $wsServerHeader -notmatch [regex]::Escape($required)) {
        $violations.Add("net_ws must retain transport diagnostics: $required")
    }
}
$wsHttpSource = Get-Content -LiteralPath `
    (Join-Path $RepoRoot "src\px_render\network\ws\http_handler.cpp") -Raw
if ($wsServerSource -notmatch "co_await\s+RedeemWsTicketAsync" -or
    $wsServerSource -notmatch "co_await\s+AdmitWsSessionAsync") {
    $violations.Add("net_ws/ws_server.cpp: websocket ticket and admission workflows must remain typed awaitables")
}
if ($wsHttpSource -notmatch "response\.defer|resp\.defer" -or
    $wsHttpSource -notmatch "AllocateLocalRtcAsync") {
    $violations.Add("net_ws/http_handler.cpp: RTC HTTP allocation must remain a deferred awaitable workflow")
}

if ($violations.Count -gt 0) {
    throw "Render architecture boundary guard failed:`n$($violations -join "`n")"
}

Write-Host "PASS: new Render architecture code is static, typed, and independent of the legacy plug-in framework."
