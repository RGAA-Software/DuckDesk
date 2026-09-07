$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$nativeRoot = Join-Path $repo 'src/px_android/core-native'
$kotlinRoot = Join-Path $nativeRoot 'src/main/java/yun/pixels/client/core/nativebridge'
$bridge = Get-Content -Raw -LiteralPath (Join-Path $kotlinRoot 'PixelsNativeBridge.kt')
$config = Get-Content -Raw -LiteralPath (Join-Path $kotlinRoot 'NativeSessionConfig.kt')
$jni = Get-Content -Raw -LiteralPath (Join-Path $nativeRoot 'src/main/cpp/jni_adapter.cpp')
$session = Get-Content -Raw -LiteralPath (Join-Path $nativeRoot 'src/main/cpp/native_session.cpp')

function Captures([string]$Source, [string]$Pattern) {
    @([regex]::Matches($Source, $Pattern) | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
}

function Assert-SameNames([string[]]$Expected, [string[]]$Actual, [string]$Boundary) {
    if (Compare-Object $Expected $Actual) { throw "Android JNI contract differs: $Boundary" }
}

Assert-SameNames (Captures $bridge 'external fun (\w+)\(') `
    (Captures $jni '\{\s*const_cast<char\*>\("(\w+)"\),') 'registered methods'
Assert-SameNames (Captures $config 'val (\w+):') `
    (Captures $jni 'Read(?:String|Int|Boolean)\(environment, config, config_class, "(\w+)"\)') 'configuration fields'
Assert-SameNames (Captures $bridge 'fun (on\w+)\(') `
    (Captures $session 'GetMethodID\([^,]+, "(on\w+)"') 'listener callback names'
Assert-SameNames (Captures $jni '(?m)^(?:jlong|jboolean|jint|void) (Native\w+)\(') `
    (Captures $jni 'pixels::android::(Native\w+)\)') 'native function bindings'

if ($config -match 'networkType|relayHost|relayPort|rtcIceConfigJson' -or
    $session -match 'params->(?:nt_type_|enable_p2p_|rtc_ice_config_json_)') {
    throw 'Android platform transport must be fixed native UDP.'
}
$gradle = Get-Content -Raw -LiteralPath (Join-Path $nativeRoot 'build.gradle')
if ($gradle -match 'webrtc-sdk|syncRtcProtos|protobuf-javalite') { throw 'Retired Android RTC build input remains.' }
Get-ChildItem -LiteralPath (Join-Path $nativeRoot 'src') -Filter '*.kt' -Recurse | ForEach-Object {
    if ((Get-Content -Raw -LiteralPath $_.FullName) -match 'import org\.webrtc|class WebRtc|class StandardRtc|class Rtc') {
        throw "Retired RTC source remains: $($_.FullName)"
    }
}
Write-Host 'PASS: Android has one native transport; JNI method, field and callback names agree.'
