$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sdk = Join-Path $repo 'src/px_client_sdk'
$connectionBuild = Get-Content -Raw -LiteralPath (Join-Path $sdk 'connection/CMakeLists.txt')
$sdkBuild = Get-Content -Raw -LiteralPath (Join-Path $sdk 'CMakeLists.txt')
$net = Get-Content -Raw -LiteralPath (Join-Path $sdk 'sdk_net_client.cpp')
$params = Get-Content -Raw -LiteralPath (Join-Path $sdk 'sdk_params.h')
$androidBuild = Get-Content -Raw -LiteralPath (Join-Path $repo 'src/px_android/core-native/src/main/cpp/CMakeLists.txt')

foreach ($retired in @('udp_connection', 'relay_connection', 'webrtc_connection', 'webrtc_local_connection', 'rtc_ice_restart_workflow')) {
    foreach ($extension in @('h', 'cpp')) {
        if (Test-Path -LiteralPath (Join-Path $sdk "connection/$retired.$extension")) {
            throw "Retired SDK source is active: $retired.$extension"
        }
    }
    if ($connectionBuild -match [regex]::Escape($retired + '.cpp')) { throw "Retired SDK build input: $retired" }
}
if ($sdkBuild -match 'px_rtc_client|px_relay_client|test_rtc_ice_restart_workflow' -or
    $androidBuild -match 'PX_RTC_TRANSPORT_AVAILABLE|px_relay_client') {
    throw 'Native SDK build graph contains a retired transport dependency.'
}
if ($net -match 'ClientNetworkType|PX_RTC_TRANSPORT_AVAILABLE|WebRtcConnection|RelayConnection' -or
    $params -match '\b(?:nt_type_|enable_p2p_|relay_host_|relay_port_|rtc_ice_config_json_)\b') {
    throw 'Native SDK still accepts a protocol selector or retired transport options.'
}
if ($net -notmatch 'MakeDirectWebSocketMediaConnection' -or $net -notmatch 'make_shared<UdpDirectConnection>') {
    throw 'Native SDK must construct WebSocket control plus UDP media.'
}
Write-Host 'PASS: Native SDK has one transport topology and no RTC/Relay/KCP source or build dependencies.'
