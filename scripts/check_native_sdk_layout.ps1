param()

$ErrorActionPreference = 'Stop'
$sdkRepo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sdkRoot = Join-Path $sdkRepo 'src/px_client_sdk'
if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot 'CMakeLists.txt') -PathType Leaf)) {
    throw 'The maintained SDK must exist at src/px_client_sdk.'
}
if (Test-Path -LiteralPath (Join-Path $sdkRepo 'src/px_deps/px_client_sdk')) {
    throw 'The former SDK directory must not contain a copy, forwarding project or symbolic link.'
}

# Restrict discovery to tracked build/maintenance inputs; archived sources are reference-only.
$sdkInputs = & git -C $sdkRepo ls-files -- '*.cmake' '*CMakeLists.txt' '*.bat' '*.ps1'
if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate build inputs.' }
$sdkFailures = [Collections.Generic.List[string]]::new()
foreach ($relative in $sdkInputs) {
    if ($relative -match '^backup[\\/]' -or $relative -in @('scripts/archive_native_sources.ps1', 'scripts/check_native_sdk_layout.ps1')) { continue }
    $inputPath = Join-Path $sdkRepo $relative
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { continue }
    $contents = Get-Content -Raw -LiteralPath $inputPath
    if ($contents -match 'px_deps[\\/]+px_client_sdk' -or $contents -match '\$\{PX_PROJECT_PATH\}[\\/]px_client_sdk') {
        $sdkFailures.Add($relative)
    }
}
if ($sdkFailures.Count -gt 0) {
    throw "Active build inputs still depend on the former SDK location: $($sdkFailures -join ', ')"
}
Write-Host 'PASS: one active SDK root; build and maintenance inputs do not refer to the former location.'
