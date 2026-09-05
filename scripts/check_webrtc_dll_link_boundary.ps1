param(
    [string]$BuildDirectory = "build_official"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $root $BuildDirectory
$ninjaPath = Join-Path $buildPath "build.ninja"

if (-not (Test-Path -LiteralPath $ninjaPath)) {
    throw "WebRTC link-boundary check requires '$ninjaPath'. Configure the focused C++ build first."
}

$ninja = Get-Content -LiteralPath $ninjaPath -Raw

function Get-LinkBlock([string]$TargetPattern) {
    $match = [regex]::Match($ninja, "(?ms)^build\s+$TargetPattern\s*:.*?(?=^build\s+|\z)")
    if (-not $match.Success) {
        throw "Unable to find link block for '$TargetPattern' in '$ninjaPath'."
    }
    return $match.Value
}

$renderLink = Get-LinkBlock "[^\r\n]*[\\/]px_render\.exe:\s+CXX_EXECUTABLE_LINKER[^\r\n]*"
foreach ($requiredImportLibrary in @("px_render_rtc_remote.lib", "px_render_rtc.lib")) {
    if ($renderLink -notmatch [regex]::Escape($requiredImportLibrary)) {
        throw "px_render.exe must link the WebRTC DLL import library '$requiredImportLibrary'."
    }
}
if ($renderLink -match "(?i)(?:^|[\\/\s])webrtc\.lib(?:\s|$)") {
    throw "px_render.exe must not link the static libwebrtc archive webrtc.lib."
}

$clientLink = Get-LinkBlock "[^\r\n]*[\\/]px_client\.exe:\s+CXX_EXECUTABLE_LINKER[^\r\n]*"
if ($clientLink -notmatch [regex]::Escape("px_client_rtc.lib")) {
    throw "px_client.exe must link the px_client_rtc.dll import library."
}
if ($clientLink -match "(?i)(?:^|[\\/\s])webrtc\.lib(?:\s|$)") {
    throw "px_client.exe must not link the static libwebrtc archive webrtc.lib."
}

foreach ($dllPattern in @(
    "[^\r\n]*[\\/]px_render_rtc_remote\.dll[^\r\n]*:\s+CXX_SHARED_LIBRARY_LINKER[^\r\n]*",
    "[^\r\n]*[\\/]px_render_rtc\.dll[^\r\n]*:\s+CXX_SHARED_LIBRARY_LINKER[^\r\n]*",
    "[^\r\n]*[\\/]px_client_rtc\.dll[^\r\n]*:\s+CXX_SHARED_LIBRARY_LINKER[^\r\n]*"
)) {
    $dllLink = Get-LinkBlock $dllPattern
    if ($dllLink -notmatch "(?i)(?:^|[\\/\s])webrtc\.lib(?:\s|$)") {
        throw "The WebRTC DLL '$dllPattern' must privately link webrtc.lib."
    }
}

$cmakeFiles = @(
    (Join-Path $root "src/px_render/network/webrtc/remote/CMakeLists.txt"),
    (Join-Path $root "src/px_render/network/webrtc/local/CMakeLists.txt"),
    (Join-Path $root "src/px_deps/px_webrtc_client/CMakeLists.txt")
)
foreach ($cmakeFile in $cmakeFiles) {
    $content = Get-Content -LiteralPath $cmakeFile -Raw
    if ($content -match "(?is)target_link_libraries\s*\([^)]*PUBLIC[^)]*webrtc\.lib") {
        throw "'$cmakeFile' exposes webrtc.lib through PUBLIC link dependencies."
    }
}

$clientRtcBoundaryFiles = @(
    (Join-Path $root "src/px_deps/px_client_sdk/connection/webrtc_connection.h"),
    (Join-Path $root "src/px_deps/px_client_sdk/connection/webrtc_connection.cpp"),
    (Join-Path $root "src/px_deps/px_client_sdk/connection/webrtc_local_connection.h"),
    (Join-Path $root "src/px_deps/px_client_sdk/connection/webrtc_local_connection.cpp"),
    (Join-Path $root "src/px_deps/px_webrtc_client/rtc_client.h"),
    (Join-Path $root "src/px_deps/px_webrtc_client/rtc_connection.h"),
    (Join-Path $root "src/px_deps/px_webrtc_client/rtc_connection.cpp")
)
foreach ($boundaryFile in $clientRtcBoundaryFiles) {
    $content = Get-Content -LiteralPath $boundaryFile -Raw
    foreach ($forbiddenSymbol in @("QLibrary", "FnGetInstance", "GetInstance", "RtcClientInterface")) {
        if ($content -match "\b$forbiddenSymbol\b") {
            throw "'$boundaryFile' reintroduced the retired WebRTC plug-in symbol '$forbiddenSymbol'."
        }
    }
}

$clientApi = Get-Content -LiteralPath (Join-Path $root "src/px_deps/px_webrtc_client/rtc_client.h") -Raw
foreach ($requiredSymbol in @("class PX_RTC_CLIENT_API RtcClient final", "std::shared_ptr<RtcClient> CreateRtcClient")) {
    if ($clientApi -notmatch [regex]::Escape($requiredSymbol)) {
        throw "Client WebRTC concrete DLL API is missing '$requiredSymbol'."
    }
}
if ($clientApi -match "RtcClientInterface|webrtc::|rtc::") {
    throw "Client WebRTC public facade must not expose an interface base or libwebrtc types."
}

Write-Host "WebRTC direct-DLL link boundary passed."
