param(
    [switch]$Staged,
    [switch]$ReportAll
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Push-Location $repoRoot
try {
    if ($ReportAll) {
        $files = & rg --files src | Where-Object {
            $_ -match '\.(cpp|cc|cxx|h|hpp)$'
        }
    }
    elseif ($Staged) {
        $files = & git diff --cached --name-only --diff-filter=ACMR -- |
            Where-Object { $_ -match '\.(cpp|cc|cxx|h|hpp)$' }
    }
    else {
        $files = & git diff --name-only --diff-filter=ACMR -- |
            Where-Object { $_ -match '\.(cpp|cc|cxx|h|hpp)$' }
    }

    $violations = [System.Collections.Generic.List[string]]::new()
    $alwaysPatterns = @(
        '(?s)\b(?:Post(?:UI|DB|Network|Bg|Work|Delay|UIDelay)?Task|QTimer::singleShot|std::(?:thread|jthread|async))\s*\(.{0,700}?\[[^\]]*\bthis\b[^\]]*\]',
        '(?s)\bconnect\s*\(.{0,500}?&Q(?:Timer::timeout|NetworkReply::finished).{0,300}?\[[^\]]*\bthis\b[^\]]*\]',
        '(?s)\.bind_(?:recv|connect|disconnect|accept|start|stop)\s*\(.{0,500}?\[[^\]]*\bthis\b[^\]]*\]'
    )
    $renderCallbackPattern =
        '(?s)\bSetOn[A-Za-z0-9_]*(?:Callback|Listener)\s*\(.{0,500}?\[[^\]]*\bthis\b[^\]]*\]'

    foreach ($file in ($files | Sort-Object -Unique)) {
        $normalized = $file -replace '\\', '/'
        if ($normalized -match '^src/px_deps/px_3rdparty/' -or
            $normalized -match '^src/px_deps/px_webrtc_client/' -or
            $normalized -match '/vigem/sdk/' -or
            $normalized -match '^src/px_panel/src/service/legacy/') {
            continue
        }
        if (-not (Test-Path -LiteralPath $file)) {
            continue
        }

        $source = Get-Content -Raw -LiteralPath $file
        $source = [regex]::Replace($source, '(?s)/\*.*?\*/', '')
        $source = [regex]::Replace($source, '(?m)^\s*//.*$', '')
        foreach ($pattern in $alwaysPatterns) {
            if ([regex]::IsMatch($source, $pattern)) {
                $violations.Add("${normalized}: raw owner capture crosses an asynchronous boundary")
                break
            }
        }
        if (($normalized -match '^src/px_render/' -or
             $normalized -match '^src/px_deps/px_relay_client/') -and
            [regex]::IsMatch($source, $renderCallbackPattern)) {
            $violations.Add("${normalized}: raw owner capture crosses a long-lived callback boundary")
        }
        if ([regex]::IsMatch($source, '\.detach\s*\(\s*\)')) {
            $violations.Add("${normalized}: detached thread bypasses RAII shutdown and join ownership")
        }
    }

    if ($violations.Count -gt 0) {
        Write-Error ("Async lifetime check failed ({0} violation(s)):`n{1}" -f
            $violations.Count, (($violations | Sort-Object -Unique) -join "`n"))
    }
    Write-Host "Async lifetime check passed: no unguarded raw owner captures at tracked async boundaries."
}
finally {
    Pop-Location
}
