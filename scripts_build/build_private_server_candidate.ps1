param(
    [string]$Distribution = 'Ubuntu-20.04',
    [string]$Output,
    [string]$PgToolchain = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Output) {
    $batchName = (Get-Date -Format 'yyyyMMdd-HHmmss')
    $Output = Join-Path $projectRoot "build_official/private_server/candidates/$batchName"
}
$candidateOutput = [System.IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $candidateOutput) {
    throw "Candidate output already exists: $candidateOutput"
}

& npm --prefix (Join-Path $projectRoot 'web/px_console') run build
if ($LASTEXITCODE -ne 0) { throw 'Console web build failed' }
& npm --prefix (Join-Path $projectRoot 'web/px_pixels') run build
if ($LASTEXITCODE -ne 0) { throw 'Desk web build failed' }

$linuxRoot = (& wsl.exe -d $Distribution -- wslpath -a $projectRoot.Replace('\', '/')).Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxRoot) { throw 'Unable to resolve the WSL source path' }
$linuxOutput = (& wsl.exe -d $Distribution -- wslpath -a $candidateOutput.Replace('\', '/')).Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxOutput) { throw 'Unable to resolve the WSL output path' }

$linuxBuildArguments = @('-d', $Distribution, '--', 'bash', "$linuxRoot/scripts_build/build_private_server_candidate.sh", $linuxOutput)
if ($PgToolchain) {
    $toolchainPath = [System.IO.Path]::GetFullPath($PgToolchain)
    if (-not (Test-Path -LiteralPath $toolchainPath -PathType Container)) { throw "PostgreSQL toolchain is unavailable: $toolchainPath" }
    $linuxToolchain = (& wsl.exe -d $Distribution -- wslpath -a $toolchainPath.Replace('\', '/')).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $linuxToolchain) { throw 'Unable to resolve the WSL PostgreSQL toolchain path' }
    $linuxBuildArguments += $linuxToolchain
}
& wsl.exe @linuxBuildArguments
if ($LASTEXITCODE -ne 0) { throw 'Linux private server candidate build failed' }
Write-Output "Private server candidate: $candidateOutput"
