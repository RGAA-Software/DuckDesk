[CmdletBinding()]
param(
    [string]$BuildDir = "build_official",
    [string]$RenderDir = "",
    [int]$PagePort = 43177,
    [int]$RenderPort = 32994,
    [int]$TimeoutSeconds = 30,
    [switch]$CpuFallback,
    [switch]$ExpectLoadFailure
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($RenderDir)) {
    $renderDir = Join-Path $repoRoot "$BuildDir\src\px_render"
} else {
    $renderDir = (Resolve-Path -LiteralPath $RenderDir).Path
}
$renderExe = Join-Path $renderDir "px_render.exe"
$pageRoot = Join-Path $repoRoot "tests\webview_e2e"
$outputRoot = Join-Path ([System.IO.Path]::GetTempPath()) "GammaRayPremium\webview-smoke"
$eventsFile = Join-Path $outputRoot "events.jsonl"
$instanceId = "smoke-$([Guid]::NewGuid().ToString('N'))"
$profilePath = Join-Path ([System.IO.Path]::GetTempPath()) "GammaRayPremium\cef\$instanceId"
$logFile = Join-Path $env:PUBLIC "Pixels\px_logs\pixels_render_$RenderPort.log"
$server = $null
$render = $null
$descendantNotBefore = $null

function Get-DescendantProcessIds([int]$RootPid) {
    $all = @(Get-CimInstance Win32_Process | Where-Object {
        -not $script:descendantNotBefore -or
        -not $_.CreationDate -or
        $_.CreationDate -ge $script:descendantNotBefore
    } | Select-Object ProcessId, ParentProcessId)
    $found = [System.Collections.Generic.HashSet[int]]::new()
    $frontier = @($RootPid)
    while ($frontier.Count -gt 0) {
        $next = @()
        foreach ($process in $all) {
            if ($frontier -contains [int]$process.ParentProcessId -and $found.Add([int]$process.ProcessId)) {
                $next += [int]$process.ProcessId
            }
        }
        $frontier = $next
    }
    return @($found)
}

try {
    if (-not (Test-Path -LiteralPath $renderExe)) {
        throw "px_render.exe not found: $renderExe"
    }
    foreach ($required in @("libcef.dll", "icudtl.dat", "resources.pak", "chrome_100_percent.pak", "locales")) {
        if (-not (Test-Path -LiteralPath (Join-Path $renderDir $required))) {
            throw "CEF runtime file is missing beside px_render.exe: $required"
        }
    }
    New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
    if (Test-Path -LiteralPath $eventsFile) { Remove-Item -LiteralPath $eventsFile -Force }
    if (Test-Path -LiteralPath $logFile) { Remove-Item -LiteralPath $logFile -Force }

    $serverArgs = @(
        (Join-Path $repoRoot "scripts\webview_e2e_server.py"),
        "--port", $PagePort,
        "--root", $pageRoot,
        "--events", $eventsFile
    )
    $server = Start-Process -FilePath "python" -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds(10)
    do {
        try { $ready = (Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:$PagePort/" -TimeoutSec 1).StatusCode -eq 200 }
        catch { $ready = $false }
        if (-not $ready) { Start-Sleep -Milliseconds 100 }
    } until ($ready -or (Get-Date) -ge $deadline)
    if (-not $ready) { throw "local WebView E2E page server did not become ready" }

    $pagePath = if ($ExpectLoadFailure) { "/missing-page.html" } else { "/" }
    $plainUrl = "http://127.0.0.1:${PagePort}${pagePath}?suite=webview&unicode=云应用&secret=must-not-appear"
    $encodedUrl = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($plainUrl)).TrimEnd('=').Replace('+', '-').Replace('/', '_')
    $renderArgs = @(
        "--app_mode=webview",
        "--webview_url_b64=$encodedUrl",
        "--webview_instance_id=$instanceId",
        "--webview_width=960",
        "--webview_height=540",
        "--webview_gpu=$(((-not $CpuFallback.IsPresent).ToString()).ToLowerInvariant())",
        "--webview_smoke_test=true",
        "--network_listen_port=$RenderPort",
        "--webrtc_enabled=false",
        "--websocket_enabled=false",
        "--capture_audio=true",
        "--capture_audio_type=inner",
        "--encoder_fps=60",
        "--logfile=true"
    )
    $descendantNotBefore = (Get-Date).AddSeconds(-2)
    $render = Start-Process -FilePath $renderExe -ArgumentList $renderArgs -WorkingDirectory $renderDir -PassThru -WindowStyle Hidden

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $log = ""
    do {
        Start-Sleep -Milliseconds 200
        if ($render.HasExited) { throw "px_render exited before producing the first WebView frame (exit=$($render.ExitCode))" }
        if (Test-Path -LiteralPath $logFile) { $log = [string]::Join("`n", @(Get-Content -LiteralPath $logFile)) }
        $firstFrame = $log.Contains("first off-screen frame is ready")
        $events = ""
        if (Test-Path -LiteralPath $eventsFile) { $events = [string]::Join("`n", @(Get-Content -LiteralPath $eventsFile)) }
        $pageReady = $events.Contains('"page-ready"')
        $audioStarted = $events.Contains('"audio-started"') -and $log.Contains("WebView CEF audio started:")
        $loadFailed = $log.Contains("WebView runtime failure:")
    } until ((($ExpectLoadFailure -and $loadFailed) -or
              (-not $ExpectLoadFailure -and $firstFrame -and $pageReady -and $audioStarted)) -or
             (Get-Date) -ge $deadline)
    if ($ExpectLoadFailure) {
        if (-not $loadFailed) { throw "the invalid main document was not reported as a WebView failure" }
        if ($firstFrame) { throw "an invalid main document was incorrectly reported ready" }
    } else {
        if (-not $firstFrame) { throw "CEF did not produce an off-screen frame within $TimeoutSeconds seconds" }
        if (-not $pageReady) { throw "the E2E page did not report page-ready within $TimeoutSeconds seconds" }
        if (-not $audioStarted) { throw "CEF page audio did not reach the host audio callback" }
    }
    if ($log.Contains("must-not-appear") -or $log.Contains($plainUrl)) { throw "decoded WebView URL leaked into px_render log" }

    $children = @(Get-DescendantProcessIds $render.Id)
    if ($children.Count -lt 2) { throw "expected CEF renderer/GPU/utility children, found $($children.Count)" }
    $leakingChild = Get-CimInstance Win32_Process | Where-Object {
        $children -contains [int]$_.ProcessId -and $_.CommandLine -like "*$encodedUrl*"
    }
    if ($leakingChild) { throw "encoded WebView URL leaked into a CEF child command line" }
    if ($ExpectLoadFailure) {
        Write-Host "PASS: failed main document was rejected without a Ready signal; CEF child count=$($children.Count)"
    } else {
        $paintMode = if ($CpuFallback) { "CPU fallback" } else { "D3D11 accelerated" }
        Write-Host "PASS: $paintMode OSR first frame, page event, and audio callback received; CEF child count=$($children.Count)"
    }
}
finally {
    if ($render -and -not $render.HasExited) {
        & taskkill.exe /PID $render.Id /T /F | Out-Null
        $render.WaitForExit(5000) | Out-Null
    }
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
        $server.WaitForExit(5000) | Out-Null
    }
    Start-Sleep -Milliseconds 300
    if ($render) {
        $leftovers = @(Get-DescendantProcessIds $render.Id | Where-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
        if ($leftovers.Count -gt 0) { throw "CEF descendant processes were not reclaimed: $($leftovers -join ', ')" }
    }
    if (Test-Path -LiteralPath $profilePath) {
        Remove-Item -LiteralPath $profilePath -Recurse -Force
    }
}
