[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MSBuild,

    [Parameter(Mandatory = $true)]
    [string]$Project,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$msbuildPath = (Resolve-Path -LiteralPath $MSBuild).Path
$projectPath = (Resolve-Path -LiteralPath $Project).Path
$projectDirectory = Split-Path -Parent $projectPath
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

# The retained WPF project writes generated XAML sources below its own obj tree.
# Serialize that third-party boundary while keeping each product's final output isolated.
$mutex = [Threading.Mutex]::new($false, 'Local\PixelsPxDisplayBuild')
$acquired = $false
try {
    $acquired = $mutex.WaitOne([TimeSpan]::FromMinutes(10))
    if (-not $acquired) {
        throw 'Timed out waiting for the px_display build boundary.'
    }

    Push-Location $projectDirectory
    try {
        & $msbuildPath $projectPath /t:Restore /p:RestoreIgnoreFailedSources=true
        if ($LASTEXITCODE -ne 0) {
            throw "px_display restore failed with exit code $LASTEXITCODE."
        }
        & $msbuildPath $projectPath /t:Build /p:Configuration=Release /p:Platform=AnyCPU `
            /p:AssemblyName=px_display "/p:OutputPath=$outputPath\"
        if ($LASTEXITCODE -ne 0) {
            throw "px_display build failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
} finally {
    if ($acquired) {
        [void]$mutex.ReleaseMutex()
    }
    $mutex.Dispose()
}

$expectedFiles = @('px_display.exe', 'px_display.exe.config')
foreach ($name in $expectedFiles) {
    $path = Join-Path $outputPath $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "px_display build did not produce $path"
    }
}
