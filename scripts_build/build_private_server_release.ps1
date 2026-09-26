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
if (Test-Path -LiteralPath $releaseOutput) {
    $existingEntries = @(Get-ChildItem -LiteralPath $releaseOutput -Name)
    $windowsOutput = Join-Path $releaseOutput 'windows'
    $windowsSetup = Join-Path $windowsOutput "PixelsServer_${nextVersion}_Setup.exe"
    $windowsManifest = Join-Path $windowsOutput 'package/sha256.json'
    if ($existingEntries.Count -ne 1 -or $existingEntries[0] -ne 'windows' -or
        -not (Test-Path -LiteralPath $windowsSetup -PathType Leaf) -or
        -not (Test-Path -LiteralPath $windowsManifest -PathType Leaf)) {
        throw "Server release output is not an isolated Windows-only version: $releaseOutput"
    }
    $manifestIdentity = Get-Content -LiteralPath $windowsManifest -Raw | ConvertFrom-Json
    if ($manifestIdentity.suite_version -ne $nextVersion -or $manifestIdentity.distribution -ne 'customer' -or
        $manifestIdentity.build_profile -ne 'optimized-release') {
        throw 'Existing Windows Server package does not match the formal suite version.'
    }
    $setupChecksumFile = "$windowsSetup.sha256"
    $expectedSetupHash = ((Get-Content -LiteralPath $setupChecksumFile -Raw).Trim() -split '\s+')[0]
    $actualSetupHash = (Get-FileHash -LiteralPath $windowsSetup -Algorithm SHA256).Hash
    if ($expectedSetupHash -ne $actualSetupHash) { throw 'Existing Windows Server Setup hash differs.' }
}

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
& wsl.exe -d $Distribution -- bash "$linuxRoot/scripts_build/build_private_server_release.sh" $linuxOutput $reservedVersion
if ($LASTEXITCODE -ne 0) { throw 'Linux private Server release build failed' }
Write-Output "Private Server Customer Compose release: $releaseOutput/PixelsServer_${reservedVersion}_Linux.tar.gz"
