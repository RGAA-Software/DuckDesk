param(
    [string]$BuildDir = "build_official",
    [string]$DistDir = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$distRoot = if ([string]::IsNullOrWhiteSpace($DistDir)) {
    Join-Path $repoRoot "$BuildDir\dist"
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $DistDir))
}

$sourceFiles = & rg --files CMakeLists.txt env_premium.cmake src scripts_build | Where-Object {
    $_ -notmatch '(^|[\\/])(backup|px_3rdparty|px_sdl3)([\\/]|$)' -and
    $_ -match '(CMakeLists\.txt|\.cmake|\.(?:h|hpp|cpp|cc|cxx))$'
}
$cmakeForbidden = '(?i)(find_package\s*\(\s*Qt|Qt[56]::|QT_ROOT|CMAKE_AUTO(?:MOC|UIC|RCC))'
$cppForbidden = '(#\s*include\s*[<"]Q[A-Z]|\bQ_OBJECT\b)'
$violations = [Collections.Generic.List[string]]::new()
foreach ($relativePath in $sourceFiles) {
    $content = Get-Content -LiteralPath (Join-Path $repoRoot $relativePath) -Raw
    $pattern = if ($relativePath -match '(CMakeLists\.txt|\.cmake)$') { $cmakeForbidden } else { $cppForbidden }
    if ($content -cmatch $pattern) {
        $violations.Add("$relativePath contains an active Qt dependency: $($Matches[0])")
    }
}
if ($violations.Count -gt 0) {
    throw ($violations -join [Environment]::NewLine)
}

if (-not (Test-Path -LiteralPath $distRoot -PathType Container)) {
    throw "dist directory does not exist: $distRoot"
}
$retiredRuntime = Get-ChildItem -LiteralPath $distRoot -File -Filter 'Qt*.dll'
$retiredDirectories = @('generic', 'iconengines', 'imageformats', 'networkinformation', 'platforms', 'styles', 'tls', 'deps\theme') |
    ForEach-Object { Join-Path $distRoot $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container }
if ($retiredRuntime -or $retiredDirectories) {
    $paths = @($retiredRuntime.FullName) + @($retiredDirectories)
    throw "retired Qt runtime remains in dist:`n$($paths -join [Environment]::NewLine)"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere.exe was not found"
}
$dumpbin = & $vswhere -latest -products * -find "VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe" | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($dumpbin)) {
    throw "dumpbin.exe was not found"
}

$products = @('px_panel.exe', 'px_client.exe', 'px_render.exe', 'px_service.exe', 'px_display.exe', 'px_function.exe', 'px_osinfo.exe')
foreach ($name in $products) {
    $artifact = Join-Path $distRoot $name
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        continue
    }
    $imports = (& $dumpbin /nologo /dependents $artifact | Out-String)
    if ($imports -match '(?im)^\s*Qt[56][^\s]*\.dll\s*$') {
        throw "$name imports Qt: $($Matches[0].Trim())"
    }
    Write-Host "ZERO-QT OK  $name"
}
