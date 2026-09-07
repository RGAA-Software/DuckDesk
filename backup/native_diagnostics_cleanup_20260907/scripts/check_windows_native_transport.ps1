$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$deviceRoot = Join-Path $repo 'src/px_panel/src/render_panel/devices'
$launchFiles = @('app_stream_list.cpp', 'running_stream_manager.cpp', 'stream_settings_dialog.cpp', 'connection_policy.h')
foreach ($name in $launchFiles) {
    $source = Get-Content -Raw -LiteralPath (Join-Path $deviceRoot $name)
    if ($source -match 'force_relay_|force_direct_|use_webrtc_|use_udp_|FallbackDirectRtc|RestartRtcSession|ResolveConnectionMode|SelectTransport') {
        throw "Retired Windows connection selector remains: $name"
    }
}
$cli = Get-Content -Raw -LiteralPath (Join-Path $repo 'src/px_client/ct_main_ws.cpp')
$launcher = Get-Content -Raw -LiteralPath (Join-Path $deviceRoot 'running_stream_manager.cpp')
foreach ($option in @('network_type', 'enable_p2p', 'force_direct', 'relay_host', 'relay_port', 'relay_appkey', 'signal_remote_device_id')) {
    if ($cli -match ("opt_" + $option + '\b') -or $launcher.Contains("--" + $option + "=")) {
        throw "Retired native CLI option remains: $option"
    }
}
if ($cli.Contains('PX_RTC_ICE_CONFIG') -or $launcher.Contains('PX_RTC_ICE_CONFIG')) { throw 'Native ICE environment routing remains.' }
$settings = Get-Content -Raw -LiteralPath (Join-Path $repo 'src/px_client/ct_settings.h')
if ($settings -notmatch 'static constexpr ClientNetworkType network_type_\{ClientNetworkType::kUdpDirect\}') {
    throw 'Windows client transport must be an immutable native UDP selection.'
}
Write-Host 'PASS: Windows settings, Panel launch and Client CLI expose no protocol selector.'
