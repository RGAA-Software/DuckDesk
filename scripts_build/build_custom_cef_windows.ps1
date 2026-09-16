[CmdletBinding()]
param(
    [string]$WorkRoot = "D:\GoCloud\cef_151",
    [string]$DepotToolsRoot = "D:\GoCloud\depot_tools",
    [string]$ProxyUrl = "http://127.0.0.1:7890"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$cefBranch = "7922"
$cefCommit = "d211df08c47ea7284a58f0106ca7a80e716f758c"
$depotToolsCommit = "94e89b10b92cc9d6e58fc8d1b6474b7d29e8a114"
$cefRepository = "https://github.com/chromiumembedded/cef.git"
$depotToolsRepository = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"
$chromiumWorkspace = Join-Path $WorkRoot "chromium_git"
$chromiumSource = Join-Path $chromiumWorkspace "chromium\src"
$logDirectory = Join-Path $WorkRoot "logs"
$bootstrapDirectory = Join-Path $WorkRoot "bootstrap_cef"
$automateScript = Join-Path $bootstrapDirectory "tools\automate\automate-git.py"

$env:HTTP_PROXY = $ProxyUrl
$env:HTTPS_PROXY = $ProxyUrl
$env:http_proxy = $ProxyUrl
$env:https_proxy = $ProxyUrl
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"
$env:DEPOT_TOOLS_UPDATE = "0"
$env:CEF_USE_GN = "1"
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
Remove-Item Env:GOROOT -ErrorAction SilentlyContinue
$env:GN_OUT_CONFIGS = "Release_GN_x64"
$env:GN_DEFINES = 'is_official_build=true is_component_build=false proprietary_codecs=true ffmpeg_branding="Chrome" chrome_pgo_phase=0 symbol_level=1 use_thin_lto=false'
$env:GIT_CONFIG_COUNT = "2"
$env:GIT_CONFIG_KEY_0 = "url.https://github.com/google-ai-edge/LiteRT.git.insteadOf"
$env:GIT_CONFIG_VALUE_0 = "https://chromium.googlesource.com/external/github.com/google-ai-edge/LiteRT.git"
$env:GIT_CONFIG_KEY_1 = "url.https://github.com/chromium/chromium.git.insteadOf"
$env:GIT_CONFIG_VALUE_1 = "https://chromium.googlesource.com/chromium/src.git"
$env:Path = "$DepotToolsRoot;$env:Path"

New-Item -ItemType Directory -Force -Path $WorkRoot, $logDirectory | Out-Null

if ((Test-Path -LiteralPath $chromiumSource) -and -not (Test-Path -LiteralPath (Join-Path $chromiumSource "chrome\VERSION"))) {
    $resolvedWorkspace = [System.IO.Path]::GetFullPath($chromiumWorkspace)
    $resolvedSource = [System.IO.Path]::GetFullPath($chromiumSource)
    if (-not $resolvedSource.StartsWith($resolvedWorkspace + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe incomplete Chromium source path: $resolvedSource"
    }

    Write-Warning "Removing incomplete Chromium source checkout: $resolvedSource"
    Remove-Item -LiteralPath $resolvedSource -Recurse -Force
}

if (-not (Test-Path (Join-Path $DepotToolsRoot ".git"))) {
    git -c "http.proxy=$ProxyUrl" clone $depotToolsRepository $DepotToolsRoot
}

git -C $DepotToolsRoot checkout --force $depotToolsCommit
if ($LASTEXITCODE -ne 0) {
    throw "Failed to pin depot_tools to $depotToolsCommit"
}

$lastChangeScript = Join-Path $chromiumSource "build\util\lastchange.py"
$lastChangeFile = Join-Path $chromiumSource "build\util\LASTCHANGE.committime"
if ((Test-Path -LiteralPath $lastChangeScript) -and -not (Test-Path -LiteralPath $lastChangeFile)) {
    & (Join-Path $DepotToolsRoot "python3.bat") $lastChangeScript -o (Join-Path $chromiumSource "build\util\LASTCHANGE")
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $lastChangeFile)) {
        throw "Chromium LASTCHANGE generation failed: $lastChangeFile"
    }
}

$dawnVersionFile = Join-Path $chromiumSource "gpu\webgpu\DAWN_VERSION"
if ((Test-Path -LiteralPath (Join-Path $chromiumWorkspace "chromium\.gclient")) -and -not (Test-Path -LiteralPath $dawnVersionFile)) {
    Push-Location (Join-Path $chromiumWorkspace "chromium")
    try {
        & (Join-Path $DepotToolsRoot "gclient.bat") runhooks --force --jobs=1 --no-nag-max
    }
    finally {
        Pop-Location
    }

    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $dawnVersionFile)) {
        throw "Chromium runhooks failed to generate $dawnVersionFile"
    }
}

if (-not (Test-Path (Join-Path $bootstrapDirectory ".git"))) {
    git -c "http.proxy=$ProxyUrl" clone --branch $cefBranch --single-branch $cefRepository $bootstrapDirectory
}

git -C $bootstrapDirectory fetch origin $cefBranch
git -C $bootstrapDirectory checkout --detach $cefCommit

if (-not (Test-Path $automateScript)) {
    throw "CEF automate script was not found at $automateScript"
}

$freeBytes = (Get-PSDrive -Name D).Free
if ($freeBytes -lt 150GB) {
    Write-Warning "D: has less than 150 GiB free. Continuing the existing x64 Release-only build. Current free bytes: $freeBytes"
}

python $automateScript `
    --download-dir=$chromiumWorkspace `
    --depot-tools-dir=$DepotToolsRoot `
    --url=$cefRepository `
    --branch=$cefBranch `
    --checkout=$cefCommit `
    --no-chromium-history `
    --force-update `
    --x64-build `
    --no-debug-build `
    --force-build `
    --force-distrib `
    --no-distrib-symbols `
    --no-distrib-docs `
    --no-distrib-archive `
    --build-log-file

if ($LASTEXITCODE -ne 0) {
    throw "CEF automate build failed with exit code $LASTEXITCODE. See build-*.log under $chromiumWorkspace"
}
