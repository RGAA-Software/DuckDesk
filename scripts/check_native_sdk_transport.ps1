$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sdk = Join-Path $repo 'src/px_client_sdk'
$connectionBuild = Get-Content -Raw -LiteralPath (Join-Path $sdk 'cmake/sdk_source_sets.cmake')
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
if (($sdkBuild + $connectionBuild) -match 'px_rtc_client|px_relay_client|test_rtc_ice_restart_workflow' -or
    $androidBuild -match 'PX_RTC_TRANSPORT_AVAILABLE|px_relay_client') {
    throw 'Native SDK build graph contains a retired transport dependency.'
}
if ($net -match 'ClientNetworkType|PX_RTC_TRANSPORT_AVAILABLE|WebRtcConnection|RelayConnection' -or
    $params -match '\b(?:nt_type_|enable_p2p_|relay_host_|relay_port_|rtc_ice_config_json_|remote_device_random_pwd_|remote_device_safety_pwd_|connection_ticket_device_id_|direct_session_grant_|direct_takeover_)\b') {
    throw 'Native SDK still accepts a protocol selector or retired transport options.'
}
if ($net -notmatch 'MakeDirectWebSocketMediaConnection' -or $net -notmatch 'make_shared<UdpDirectConnection>') {
    throw 'Native SDK must construct WebSocket control plus UDP media.'
}
$messages = Get-Content -Raw -LiteralPath (Join-Path $sdk 'sdk_messages.h')
$statistics = Get-Content -Raw -LiteralPath (Join-Path $sdk 'sdk_statistics.h')
$api = Get-Content -Raw -LiteralPath (Join-Path $sdk 'thunder_sdk.h')
if ($messages -match 'SdkMsgRtc|SdkMsgRelay|SdkMsgRoom|SdkMsgRemoteIce|SdkMsgRemoteAnswerSdp|ClientNetworkType|SdkMsgReconnect' -or
    $statistics -match 'rtc_ice_|rtc_signaling_|rtc_local_candidate_|rtc_remote_candidate_|rtc_turn_|rtc_rtt_|rtc_available_' -or
    $api -match 'RetryConnection|GetProgressSteps|ReportStatistics') {
    throw 'Native SDK retains retired RTC diagnostics, fake retry, or UI progress APIs.'
}
foreach ($retiredGl in @('director', 'sprite', 'renderer', 'shader_program', 'gl_function', 'video_widget_shaders')) {
    foreach ($extension in @('h', 'cpp')) {
        if (Test-Path -LiteralPath (Join-Path $sdk "gl/$retiredGl.$extension")) {
            throw "Unused SDK presentation helper remains: $retiredGl.$extension"
        }
    }
}
if ($sdkBuild -match '(?s)target_link_libraries\(px_sdk(?:_core|_platform)?\b[^)]*Qt6::' -or
    $sdkBuild -notmatch 'set\(sdk_targets px_sdk_core\)' -or
    $sdkBuild -notmatch 'list\(APPEND sdk_targets px_sdk_platform px_sdk\)' -or
    $sdkBuild -notmatch 'foreach\(sdk_target IN LISTS sdk_targets\)' -or
    $sdkBuild -notmatch 'set_target_properties\(\$\{sdk_target\} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF\)') {
    throw 'Native SDK must not link Qt or enable Qt code generation.'
}
Get-ChildItem -LiteralPath $sdk -Recurse -File | Where-Object {
    $_.Extension -in @('.h', '.cpp') -and $_.FullName -notmatch '[\\/]tests[\\/]'
} | ForEach-Object {
    if ((Get-Content -Raw -LiteralPath $_.FullName) -match '(?m)^\s*#\s*include\s*[<"]Q\w+') {
        throw "SDK production source includes Qt: $($_.FullName)"
    }
}
& cmake -P (Join-Path $sdk 'tests/test_sdk_source_sets.cmake')
if ($LASTEXITCODE -ne 0) { throw 'SDK build-layer source boundaries failed.' }
Write-Host 'PASS: Native SDK has one transport topology and no RTC/Relay/KCP source or build dependencies.'
