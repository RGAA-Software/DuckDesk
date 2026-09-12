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
if ($settings -match 'network_type_|IsRelayMode|IsDirectMode|rtc_ice_config_json_|enable_p2p_|signal_remote_device_id_|relay_host_|force_direct_') {
    throw 'Windows client settings must not retain retired transport fields or classifiers.'
}
$statistics = Get-Content -Raw -LiteralPath (Join-Path $repo 'src/px_client/ui/ct_statistics_panel.cpp')
if ($statistics -match 'lbl_rtc_|ClientNetworkType|ICE Path|TURN / RTT' -or $statistics -notmatch 'UDP/FEC \+ WS') {
    throw 'Windows diagnostics must describe the native transport without obsolete ICE/TURN rows.'
}
Write-Host 'PASS: Windows settings, Panel launch and Client CLI expose no protocol selector.'
