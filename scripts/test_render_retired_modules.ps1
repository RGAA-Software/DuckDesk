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

function Assert-Match {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Reason
    )
    if ((Get-Content -LiteralPath $Path -Raw) -notmatch $Pattern) {
        throw "$Reason ($Path)"
    }
}

function Assert-NotMatch {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Reason
    )
    if ((Get-Content -LiteralPath $Path -Raw) -match $Pattern) {
        throw "$Reason ($Path)"
    }
}

$legacySourceDirectory = Join-Path $RepoRoot "src\px_render\plugins"
if (Test-Path -LiteralPath $legacySourceDirectory) {
    throw "legacy Render plugins source directory still exists: $legacySourceDirectory"
}

$renderRootCmake = Join-Path $RepoRoot "src\px_render\CMakeLists.txt"
$architectureCmake = Join-Path $RepoRoot "src\px_render\architecture\CMakeLists.txt"
$publisher = Join-Path $RepoRoot "scripts\publish_cpp_artifacts.ps1"
$collector = Join-Path $RepoRoot "scripts\collect_dist.py"

Assert-NotMatch -Path $renderRootCmake `
    -Pattern 'add_subdirectory\s*\(\s*plugins\s*\)' `
    -Reason "Render still adds the legacy plugins source tree"
Assert-Match -Path $renderRootCmake `
    -Pattern 'add_subdirectory\s*\(\s*network/webrtc/remote\s*\)' `
    -Reason "remote WebRTC library is not in the network build layer"
Assert-Match -Path $renderRootCmake `
    -Pattern 'add_subdirectory\s*\(\s*network/webrtc/local\s*\)' `
    -Reason "local WebRTC library is not in the network build layer"

foreach ($retiredModule in @("mock_video_stream", "obj_detector")) {
    $tracked = @(& git -C $RepoRoot ls-files -- "src/px_render") |
        Where-Object { $_ -match [regex]::Escape($retiredModule) }
    if ($tracked) {
        throw "retired Render source is still tracked: $($tracked -join ', ')"
    }
}

foreach ($requiredSource in @(
    "observers/frame_debugger_observer.cpp",
    "processors/frame_resizer_processor.cpp",
    "processors/frame_carrier_processor.cpp",
    "encoders/opus/opus_encoder_runtime.cpp",
    "sources/was_audio/was_audio_capture_runtime.cpp",
    "services/file_transfer_service.cpp",
    "services/event_replayer/win_event_replayer.cpp",
    "sinks/live_pusher/live_pusher_ffmpeg.cpp"
)) {
    Assert-Match -Path $architectureCmake `
        -Pattern ([regex]::Escape($requiredSource)) `
        -Reason "built-in Render implementation is missing from the static graph: $requiredSource"
}

foreach ($script in @($publisher, $collector)) {
    Assert-Match -Path $script `
        -Pattern 'rd_plugins' `
        -Reason "legacy rd_plugins cleanup is missing"
}
Assert-Match -Path $publisher -Pattern 'deps\\network' `
    -Reason "focused publishing does not target deps/network"
Assert-Match -Path $collector -Pattern 'network_dst' `
    -Reason "full dist collection does not target deps/network"

if ($CheckDist) {
    $legacyDistDirectory = Join-Path $RepoRoot "build_official\dist\deps\rd_plugins"
    if (Test-Path -LiteralPath $legacyDistDirectory) {
        throw "legacy Render plugin delivery directory still exists: $legacyDistDirectory"
    }
    $networkDirectory = Join-Path $RepoRoot "build_official\dist\deps\network"
    foreach ($library in @("net_rtc.dll", "net_rtc_local.dll")) {
        $path = Join-Path $networkDirectory $library
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "required WebRTC network library is missing: $path"
        }
    }
}

Write-Host "PASS: Render production source and delivery no longer use a plugins tree."
