param(
    [ValidateSet("quick", "lifecycle", "integration", "hardware", "all", "performance")]
    [string]$Mode = "all",

    [ValidateRange(1, 64)]
    [int]$Jobs = 8
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = Join-Path $repoRoot "build_official"
$runId = "{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $Mode
$evidenceRoot = Join-Path $repoRoot "test-results/render-architecture/$runId"
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $evidenceRoot "e2e") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $evidenceRoot "performance") -Force | Out-Null

function Write-Utf8File {
    param([string]$Path, [object[]]$Value)
    $Value | Set-Content -LiteralPath $Path -Encoding utf8
}

function Invoke-ExternalLogged {
    param(
        [string]$LogPath,
        [scriptblock]$Command
    )
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Command 2>&1 |
            Tee-Object -FilePath $LogPath |
            ForEach-Object { Write-Host $_ }
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    return $exitCode
}

function Get-TestCounts {
    param([string]$Path)
    $counts = [ordered]@{ passed = 0; failed = 0; skipped = 0; total = 0 }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $counts
    }
    try {
        [xml]$report = Get-Content -Raw -LiteralPath $Path
        $suite = $report.testsuites.testsuite
        if (-not $suite) {
            $suite = $report.testsuite
        }
        foreach ($entry in @($suite)) {
            $tests = [int]$entry.tests
            $failures = [int]$entry.failures + [int]$entry.errors
            $skipped = [int]$entry.skipped + [int]$entry.disabled
            $counts.total += $tests
            $counts.failed += $failures
            $counts.skipped += $skipped
            $counts.passed += $tests - $failures - $skipped
        }
    }
    catch {
        Write-Warning "Unable to parse JUnit report ${Path}: $($_.Exception.Message)"
    }
    return $counts
}

function Get-Sha256 {
    param([string]$Path)
    $stream = [System.IO.File]::OpenRead($Path)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $algorithm.ComputeHash($stream)
        return ([System.BitConverter]::ToString($bytes) -replace "-", "")
    }
    finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

$unitTargets = @(
    "test_render_architecture_core",
    "test_captured_media_pipeline",
    "test_network_transport_hub",
    "test_ws_callback_workflow",
    "test_frame_resizer_processor",
    "test_frame_carrier_processor",
    "test_opus_encoder_processor",
    "test_input_replay_service",
    "test_joystick_service",
    "test_file_transfer_service",
    "test_render_service_rpc_state",
    "test_logical_session_registry",
    "test_direct_session_grant_store",
    "test_voice_call_service"
)
$lifecycleTargets = @(
    "test_captured_media_pipeline",
    "test_ws_callback_workflow",
    "test_media_recorder_sink",
    "test_live_pusher_sink",
    "test_was_audio_capture_source",
    "test_was_audio_capture_runtime",
    "test_miniaudio_reinit_cancel",
    "test_process_loopback_lifecycle",
    "test_render_execution_context_lifecycle",
    "test_callback_quiescence",
    "test_reconnect_supervisor",
    "test_sdk_websocket_reconnect",
    "test_relay_ws_reconnect",
    "test_relay_transport_reconnect_owner",
    "test_udp_transport_shutdown",
    "test_ws_ipc_client_lifecycle",
    "test_webrtc_transport_lifecycle",
    "test_rtc_client_dll_lifecycle",
    "test_voice_call_runtime"
)
$integrationTargets = @(
    "test_live_pusher_ffmpeg",
    "test_opus_encoder_runtime",
    "test_render_builtin_linkage",
    "test_record_writer",
    "test_voice_call"
)
$hardwareTargets = @(
    "test_was_audio_capture_hardware",
    "test_miniaudio_pid_loopback"
)

$targets = switch ($Mode) {
    "quick" { $unitTargets }
    "lifecycle" { $lifecycleTargets }
    "integration" { $unitTargets + $lifecycleTargets + $integrationTargets + "px_render" }
    "hardware" { $hardwareTargets }
    "all" { $unitTargets + $lifecycleTargets + $integrationTargets + "px_render" }
    "performance" { @("test_render_architecture_core") }
}
$targets = @($targets)
$targets = @($targets + "check_cpp_ownership" | Select-Object -Unique)

$environmentLines = [System.Collections.Generic.List[string]]::new()
$environmentLines.Add("run_id=$runId")
$environmentLines.Add("mode=$Mode")
$environmentLines.Add("timestamp=$(Get-Date -Format o)")
$environmentLines.Add("computer=$env:COMPUTERNAME")
try {
    $os = Get-CimInstance Win32_OperatingSystem
    $environmentLines.Add("os=$($os.Caption) $($os.Version) build=$($os.BuildNumber)")
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    $environmentLines.Add("cpu=$($cpu.Name)")
    foreach ($gpu in @(Get-CimInstance Win32_VideoController)) {
        $environmentLines.Add("gpu=$($gpu.Name) driver=$($gpu.DriverVersion)")
    }
}
catch {
    $environmentLines.Add("hardware_inventory=unavailable reason=$($_.Exception.Message)")
}
$environmentLines.Add("external_lan_e2e=not_started")
$environmentLines.Add("media_profile=provided_by_acceptance_environment")
Write-Utf8File (Join-Path $evidenceRoot "environment.txt") $environmentLines
Write-Utf8File (Join-Path $evidenceRoot "git-revision.txt") @(
    "commit=$(& git -C $repoRoot rev-parse HEAD)",
    "branch=$(& git -C $repoRoot branch --show-current)"
)
Write-Utf8File (Join-Path $evidenceRoot "build-targets.txt") $targets
Write-Utf8File (Join-Path $evidenceRoot "process-metrics.csv") @(
    "timestamp,working_set_bytes,private_bytes,thread_count,handle_count,note",
    "$(Get-Date -Format o),,,,,render_not_started_by_architecture_runner"
)
$notRunPerformance = [ordered]@{
    status = "not_run"
    reason = "fixed acceptance hardware and media profile required"
    mode = $Mode
}
Write-Utf8File (Join-Path $evidenceRoot "performance/baseline.json") @(
    $notRunPerformance | ConvertTo-Json
)
Write-Utf8File (Join-Path $evidenceRoot "performance/comparison.json") @(
    $notRunPerformance | ConvertTo-Json
)

$overallExit = 0
$env:CPP_BUILD_JOBS = $Jobs.ToString()
$builder = Join-Path $repoRoot "scripts/build_cpp_target.bat"
$buildLog = Join-Path $evidenceRoot "build.log"
$buildExit = Invoke-ExternalLogged $buildLog { & $builder @targets }
if ($buildExit -ne 0) {
    $overallExit = $buildExit
}

$gateExit = 1
$testExit = 1
$testSkippedReason = ""
$gateReport = Join-Path $evidenceRoot "gates.xml"
$testReport = Join-Path $evidenceRoot "ctest.xml"
if ($buildExit -eq 0) {
    $gateLog = Join-Path $evidenceRoot "gate.log"
    $gateExit = Invoke-ExternalLogged $gateLog {
        & ctest --test-dir $buildRoot -L "^render-guard$" --output-on-failure --output-junit $gateReport
    }
    if ($gateExit -eq 0) {
        $asyncLog = Join-Path $evidenceRoot "async-lifetime.log"
        $gateExit = Invoke-ExternalLogged $asyncLog {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot "scripts/check_async_lifetime.ps1") -ReportAll
        }
    }
    if ($gateExit -ne 0) {
        $overallExit = $gateExit
    }

    $label = switch ($Mode) {
        "quick" { "^render-unit$" }
        "lifecycle" { "^render-lifecycle$" }
        "integration" { "^render-(unit|lifecycle|integration)$" }
        "hardware" { "^render-hardware$" }
        "all" { "^render-(unit|lifecycle|integration)$" }
        "performance" { "^render-performance$" }
    }
    $discovery = & ctest --test-dir $buildRoot -N -L $label 2>&1
    $testCount = 0
    if (($discovery -join "`n") -match "Total Tests:\s+(\d+)") {
        $testCount = [int]$Matches[1]
    }
    $testLog = Join-Path $evidenceRoot ("{0}.log" -f $Mode)
    if ($testCount -eq 0) {
        $testSkippedReason = "No automated $Mode test is registered; fixed hardware/profile acceptance is required."
        Write-Utf8File $testLog @("SKIP: $testSkippedReason")
        Write-Utf8File $testReport @('<?xml version="1.0" encoding="UTF-8"?><testsuites tests="0" failures="0" skipped="0"/>')
        $testExit = 0
    }
    else {
        $testExit = Invoke-ExternalLogged $testLog {
            & ctest --test-dir $buildRoot -L $label --parallel $Jobs --output-on-failure --output-junit $testReport
        }
    }
    if ($testExit -ne 0) {
        $overallExit = $testExit
    }
}

$privacyLog = Join-Path $evidenceRoot "log-privacy-scan.txt"
$privacyViolations = [System.Collections.Generic.List[string]]::new()
$unexpectedErrorCount = 0
$sensitiveAssignment = '(?i)\b(?:appkey|ticket|nonce|password|cookie|authorization|ice_(?:ufrag|pwd|credential))\s*[:=]\s*(?!<redacted>|<none>|missing\b)[^\s,;]+'
$queryValueLeak = '(?i)\bk\s*:\s*(?:appkey|ticket|nonce|password|cookie|authorization)\s*,\s*v\s*:\s*(?!<redacted>)[^\s,;]+'
foreach ($logFile in @(Get-ChildItem -LiteralPath $evidenceRoot -Filter "*.log" -File)) {
    $lineNumber = 0
    foreach ($line in @(Get-Content -LiteralPath $logFile.FullName)) {
        ++$lineNumber
        if ($line -match '(?i)\[ERROR\]' -or
            $line -match '(?i)\bevent=\S+.*\boutcome=(?:failed|timeout)\b') {
            ++$unexpectedErrorCount
        }
        if ($line -match $sensitiveAssignment -or $line -match $queryValueLeak) {
            $privacyViolations.Add("$($logFile.Name):$lineNumber potential sensitive value")
        }
    }
}
if ($privacyViolations.Count -eq 0) {
    Write-Utf8File $privacyLog @("PASS: no credential-shaped values were found in runner logs.")
}
else {
    Write-Utf8File $privacyLog (@("FAIL: potential sensitive values found.") + $privacyViolations)
    $overallExit = 1
}
if ($unexpectedErrorCount -gt 0) {
    $overallExit = 1
}

$artifactLines = [System.Collections.Generic.List[string]]::new()
$artifactMismatch = $false
$artifactPairs = @(
    @("px_render.exe", "src/px_render/px_render.exe", "dist/px_render.exe"),
    @("px_gh.dll", "src/px_render/hook_capture/win/hk_obs/px_gh.dll", "dist/px_gh.dll"),
    @("px_render_rtc_remote.dll", "src/px_render/network/webrtc/remote/px_render_rtc_remote.dll", "dist/px_render_rtc_remote.dll"),
    @("px_render_rtc.dll", "src/px_render/network/webrtc/local/px_render_rtc.dll", "dist/px_render_rtc.dll")
)
foreach ($pair in $artifactPairs) {
    $sourcePath = Join-Path $buildRoot $pair[1]
    $destinationPath = Join-Path $buildRoot $pair[2]
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $destinationPath -PathType Leaf)) {
        $artifactLines.Add("$($pair[0]) status=SKIP reason=artifact_missing")
        continue
    }
    $sourceHash = Get-Sha256 $sourcePath
    $destinationHash = Get-Sha256 $destinationPath
    $match = $sourceHash -eq $destinationHash
    $artifactLines.Add("$($pair[0]) build=$sourceHash dist=$destinationHash match=$match")
    if (-not $match) {
        $artifactMismatch = $true
    }
}
Write-Utf8File (Join-Path $evidenceRoot "artifact-hashes.txt") $artifactLines
if ($artifactMismatch) {
    $overallExit = 1
}

$gateCounts = Get-TestCounts $gateReport
$testCounts = Get-TestCounts $testReport
$knownIssues = [System.Collections.Generic.List[string]]::new()
if ($Mode -notin @("hardware", "performance")) {
    $knownIssues.Add("Hardware, LAN E2E, 30-minute pressure, and 8-hour soak remain acceptance-environment runs.")
}
if ($testCounts.skipped -gt 0) {
    $knownIssues.Add("$($testCounts.skipped) test(s) skipped because acceptance prerequisites were not supplied; see the mode log.")
}
if ($testSkippedReason) {
    $knownIssues.Add($testSkippedReason)
}
$decision = if ($overallExit -ne 0) {
    "NO-GO"
}
elseif ($testCounts.skipped -gt 0 -or $testSkippedReason) {
    "INCOMPLETE: automated gates passed, but acceptance prerequisites were skipped."
}
else {
    "GO for the completed automated software gate; not final product acceptance."
}
$summary = @(
    "# Render architecture test summary",
    "",
    "- Run: $runId",
    "- Mode: $Mode",
    "- Build exit: $buildExit",
    "- Gate exit: $gateExit",
    "- Test exit: $testExit",
    "- PASS: $($gateCounts.passed + $testCounts.passed)",
    "- FAIL: $($gateCounts.failed + $testCounts.failed)",
    "- SKIP: $($gateCounts.skipped + $testCounts.skipped)",
    "- Privacy scan: $(if ($privacyViolations.Count -eq 0) { 'PASS' } else { 'FAIL' })",
    "- Artifact hashes: $(if ($artifactMismatch) { 'FAIL' } else { 'PASS or explicitly missing' })",
    "- Unexpected ERROR count: $unexpectedErrorCount",
    "- Decision: $decision",
    "",
    "## Known issues / external acceptance",
    ""
)
if ($knownIssues.Count -eq 0) {
    $summary += "- None."
}
else {
    $summary += @($knownIssues | ForEach-Object { "- $_" })
}
Write-Utf8File (Join-Path $evidenceRoot "summary.md") $summary

Write-Host "Evidence: $evidenceRoot"
exit $overallExit
