#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [ValidateSet('server', 'cloud_node')] [string]$Product,
    [Parameter(Mandatory)] [string]$SetupPath,
    [Parameter(Mandatory)] [string]$SetupSha256,
    [Parameter(Mandatory)] [string]$ManifestSha256,
    [Parameter(Mandatory)] [string]$ExpectedVersion,
    [Parameter(Mandatory)] [string]$InstallRoot
)

$ErrorActionPreference = 'Stop'
Import-Module -Name (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')

function Assert-FileHash([string]$FilePath, [string]$ExpectedHash) {
    if ($ExpectedHash -notmatch '^[a-f0-9]{64}$') { throw 'Invalid expected SHA-256.' }
    $actualHash = (Get-FileHash -LiteralPath $FilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -cne $ExpectedHash) { throw "SHA-256 mismatch: $FilePath" }
}

Assert-FileHash $SetupPath $SetupSha256
Write-Output "SETUP_VERIFIED $Product $ExpectedVersion"
$installerProcessInfo = New-Object Diagnostics.ProcessStartInfo
$installerProcessInfo.FileName = $SetupPath
$installerProcessInfo.Arguments = '/S'
$installerProcessInfo.UseShellExecute = $false
$installerProcessInfo.CreateNoWindow = $true
# Use native Windows modules, not optional SQL Server modules in the executor's profile.
$installerProcessInfo.EnvironmentVariables['PSModulePath'] = Join-Path $PSHOME 'Modules'
$installerProcess = New-Object Diagnostics.Process
$installerProcess.StartInfo = $installerProcessInfo
try {
    if (-not $installerProcess.Start()) { throw 'Installer did not start.' }
    Write-Output ("SETUP_STARTED $Product pid=" + $installerProcess.Id)
    $installerProcess.WaitForExit()
    Write-Output ("SETUP_EXIT $Product code=" + $installerProcess.ExitCode)
    if ($installerProcess.ExitCode -ne 0) { throw 'Product Setup failed; do not replace files manually.' }
} finally { $installerProcess.Dispose() }

$manifestPath = if ($Product -eq 'server') {
    Join-Path $InstallRoot 'current/sha256.json'
} else {
    Join-Path $InstallRoot 'product-manifest.json'
}
Assert-FileHash $manifestPath $ManifestSha256
$installedManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($Product -eq 'server') {
    if ($installedManifest.product -cne 'pixels-single-server' -or
        $installedManifest.distribution -cne 'official' -or
        $installedManifest.suite_version -cne $ExpectedVersion) { throw 'Installed Server identity mismatch.' }
    $runtimeRoot = Join-Path $InstallRoot 'current'
    foreach ($runtimeEntry in $installedManifest.files.PSObject.Properties) {
        Assert-FileHash (Join-Path $runtimeRoot $runtimeEntry.Name) ([string]$runtimeEntry.Value)
    }
    $requiredServiceNames = @('Pixels.Console', 'Pixels.Relay')
    $backupServices = @(Get-Service -Name 'Pixels.Backup.*' -ErrorAction SilentlyContinue)
    if ($backupServices.Count -ne 1) { throw 'Expected exactly one Server Backup service.' }
    $requiredServiceNames += $backupServices[0].Name
} else {
    if ($installedManifest.product -cne 'cloud_node' -or
        $installedManifest.distribution -cne 'official' -or
        $installedManifest.product_version -cne $ExpectedVersion) { throw 'Installed Cloud Node identity mismatch.' }
    foreach ($runtimeArtifact in $installedManifest.artifacts) {
        Assert-FileHash (Join-Path $InstallRoot $runtimeArtifact.path) ([string]$runtimeArtifact.sha256).ToLowerInvariant()
    }
    $requiredServiceNames = @('px_service')
}
foreach ($serviceName in $requiredServiceNames) {
    $installedService = Get-Service -Name $serviceName
    if ($installedService.Status -ne 'Running') { throw "Service is not running: $serviceName" }
    Write-Output "SERVICE_RUNNING $serviceName"
}
Write-Output "INSTALLED_VERIFIED $Product $ExpectedVersion"
