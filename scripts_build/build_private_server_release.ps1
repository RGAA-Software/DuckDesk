param(
    [string]$Distribution = 'Ubuntu-20.04'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$versionScript = Join-Path $projectRoot 'scripts/server_private/server_suite_version.py'
$nextVersion = (& python $versionScript).Trim()
if ($LASTEXITCODE -ne 0 -or -not $nextVersion) { throw 'Unable to read the next Server suite version' }
$releaseRoot = Join-Path $projectRoot 'build_official/private_server/customer'
$releaseOutput = Join-Path $releaseRoot $nextVersion
if (Test-Path -LiteralPath $releaseOutput) { throw "Server release already exists: $releaseOutput" }
if (Test-Path -LiteralPath "$releaseOutput.tar.gz") { throw "Server archive already exists: $releaseOutput.tar.gz" }

$linuxRoot = (& wsl.exe -d $Distribution -- wslpath -a $projectRoot.Replace('\', '/')).Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxRoot) { throw 'Unable to resolve the WSL source path' }
$linuxOutput = (& wsl.exe -d $Distribution -- wslpath -a $releaseOutput.Replace('\', '/')).Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxOutput) { throw 'Unable to resolve the WSL release output' }

$reservedVersion = (& python $versionScript --reserve $nextVersion).Trim()
if ($LASTEXITCODE -ne 0 -or $reservedVersion -ne $nextVersion) { throw 'Unable to reserve the Server suite version' }
Write-Output "Reserved Server suite version $reservedVersion; failed builds do not reuse it."

& npm --prefix (Join-Path $projectRoot 'web/px_console') ci
if ($LASTEXITCODE -ne 0) { throw 'Console web dependency installation failed' }
& npm --prefix (Join-Path $projectRoot 'web/px_console') run build
if ($LASTEXITCODE -ne 0) { throw 'Console web build failed' }
& npm --prefix (Join-Path $projectRoot 'web/px_pixels') ci
if ($LASTEXITCODE -ne 0) { throw 'Desk web dependency installation failed' }
& npm --prefix (Join-Path $projectRoot 'web/px_pixels') run build
if ($LASTEXITCODE -ne 0) { throw 'Desk web build failed' }

& wsl.exe -d $Distribution -- bash "$linuxRoot/scripts_build/build_private_server_release.sh" $linuxOutput $reservedVersion
if ($LASTEXITCODE -ne 0) { throw 'Linux private Server release build failed' }
Write-Output "Private Server Customer release: $releaseOutput.tar.gz"
