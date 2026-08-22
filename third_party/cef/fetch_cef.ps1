[CmdletBinding()]
param(
    [string]$DestinationRoot = '',
    [string]$CacheRoot = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (-not $DestinationRoot) { $DestinationRoot = $PSScriptRoot }
if (-not $CacheRoot) {
    $localCacheBase = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { $env:TEMP }
    $CacheRoot = Join-Path $localCacheBase 'GammaRayPremium\cef-cache'
}

$manifestPath = Join-Path $PSScriptRoot 'manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (-not $manifest.url.StartsWith('https://cef-builds.spotifycdn.com/')) {
    throw 'CEF manifest URL must use the official cef-builds.spotifycdn.com host.'
}

$distributionName = [IO.Path]::GetFileNameWithoutExtension(
    [IO.Path]::GetFileNameWithoutExtension($manifest.archive)
)
$destination = Join-Path $DestinationRoot $distributionName
$marker = Join-Path $destination '.cef-ready'
if ((Test-Path -LiteralPath $marker) -and -not $Force) {
    Write-Host "CEF is ready: $destination"
    Write-Output $destination
    exit 0
}

New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
New-Item -ItemType Directory -Force -Path $DestinationRoot | Out-Null
$archivePath = Join-Path $CacheRoot $manifest.archive

function Test-CefArchive {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA1).Hash.ToLowerInvariant()
    return $actual -eq $manifest.sha1.ToLowerInvariant()
}

if (-not (Test-CefArchive -Path $archivePath) -or $Force) {
    $partialPath = "$archivePath.partial"
    $proxy = $env:HTTPS_PROXY
    if (-not $proxy) { $proxy = $env:HTTP_PROXY }
    if (-not $proxy) { $proxy = git config --global --get http.proxy 2>$null }

    $curlArgs = @('--fail', '--location', '--retry', '5', '--retry-delay', '2',
                  '--continue-at', '-', '--output', $partialPath)
    if ($proxy) { $curlArgs += @('--proxy', $proxy) }
    $curlArgs += $manifest.url
    Write-Host "Downloading CEF $($manifest.version) ..."
    & curl.exe @curlArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CEF download failed with curl exit code $LASTEXITCODE."
    }
    Move-Item -LiteralPath $partialPath -Destination $archivePath -Force
}

if (-not (Test-CefArchive -Path $archivePath)) {
    throw 'CEF archive checksum mismatch.'
}

if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
Write-Host "Extracting CEF to $DestinationRoot ..."
$gitUsrBin = 'C:\Program Files\Git\usr\bin'
$bzip2 = Join-Path $gitUsrBin 'bzip2.exe'
if (-not (Test-Path -LiteralPath $bzip2)) {
    throw 'bzip2.exe was not found. Install Git for Windows or provide bzip2 on this machine.'
}
$systemTar = Join-Path $env:SystemRoot 'System32\tar.exe'
$tarCommand = if (Test-Path -LiteralPath $systemTar) { $systemTar } else { 'tar.exe' }
$tarArchive = $archivePath.Substring(0, $archivePath.Length - '.bz2'.Length)
if (Test-Path -LiteralPath $tarArchive) {
    Remove-Item -LiteralPath $tarArchive -Force
}
& $bzip2 -dkf $archivePath
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $tarArchive)) {
    throw "CEF bzip2 decompression failed with exit code $LASTEXITCODE."
}
& $tarCommand -xf $tarArchive -C $DestinationRoot
$tarExitCode = $LASTEXITCODE
Remove-Item -LiteralPath $tarArchive -Force
if ($tarExitCode -ne 0) {
    throw "CEF extraction failed with tar exit code $tarExitCode."
}
if (-not (Test-Path -LiteralPath (Join-Path $destination 'CMakeLists.txt'))) {
    throw "CEF extraction did not create the expected directory: $destination"
}

Set-Content -LiteralPath $marker -Value $manifest.sha1 -Encoding ascii
Write-Host "CEF is ready: $destination"
Write-Output $destination
