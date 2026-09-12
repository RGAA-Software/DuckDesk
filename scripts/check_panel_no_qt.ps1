param(
    [string]$BuildDir = "build_official",
    [string]$DistDir = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = Join-Path $repoRoot $BuildDir
$distRoot = if ([string]::IsNullOrWhiteSpace($DistDir)) { Join-Path $buildRoot "dist" } else { Join-Path $repoRoot $DistDir }
$panel = Join-Path $distRoot "px_panel.exe"
if (-not (Test-Path -LiteralPath $panel -PathType Leaf)) { throw "Panel artifact is missing: $panel" }

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw "vswhere.exe was not found" }
$dumpbin = & $vswhere -latest -products * -find "VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe" | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($dumpbin)) { throw "dumpbin.exe was not found" }

$imports = (& $dumpbin /nologo /dependents $panel | Out-String)
if ($imports -match '(?im)^\s*Qt\d[^\s]*\.dll\s*$') { throw "px_panel.exe imports Qt: $($Matches[0].Trim())" }

$responseFile = Join-Path $buildRoot "src\px_panel\ui_imgui\CMakeFiles\px_panel.rsp"
if (Test-Path -LiteralPath $responseFile -PathType Leaf) {
    $linkClosure = Get-Content -LiteralPath $responseFile -Raw
    if ($linkClosure -match '(?i)(Qt[56][A-Za-z0-9_]*\.lib|Qt[56]::)') { throw "px_panel link closure contains Qt: $($Matches[0])" }
}

Write-Host "ZERO-QT OK  px_panel.exe"
