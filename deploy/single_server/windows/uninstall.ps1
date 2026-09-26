#requires -Version 5.1
[CmdletBinding()]
param([string]$InstallRoot = "$env:ProgramFiles\Pixels\Server")

$ErrorActionPreference = 'Stop'
$resolvedInstall = [IO.Path]::GetFullPath($InstallRoot)
if (-not [IO.Path]::IsPathRooted($InstallRoot) -or $resolvedInstall -eq [IO.Path]::GetPathRoot($resolvedInstall)) {
    throw 'Install root is unsafe.'
}
$currentPath = Join-Path $resolvedInstall 'current'
$manifestPath = Join-Path $currentPath 'sha256.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    $currentPath = Join-Path $resolvedInstall 'setup_payload'
    $manifestPath = Join-Path $currentPath 'sha256.json'
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Installed package manifest is missing.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.product -cne 'pixels-single-server' -or $manifest.distribution -cne 'customer' -or
    $manifest.platform -cne 'windows-x86_64') { throw 'Installed product identity differs.' }
$serviceNames = @('Pixels.Setup', 'Pixels.Console', 'Pixels.Relay')
$serviceNames += @(Get-Service -Name 'Pixels.Backup.*' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
foreach ($serviceName in $serviceNames) {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -eq $service) { continue }
    $installedCommand = [string](Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$serviceName" -Name ImagePath).ImagePath
    $expectedExecutable = if ($serviceName -eq 'Pixels.Setup') { 'px_console_admin.exe' } elseif ($serviceName -like 'Pixels.Backup.*') { 'px_backup.exe' } elseif ($serviceName -eq 'Pixels.Console') {
        'px_console.exe'
    } else { 'px_relay.exe' }
    $expectedPath = Join-Path (Join-Path $currentPath 'bin') $expectedExecutable
    $quotedCommandMatches = $installedCommand.StartsWith("`"$expectedPath`" ", [StringComparison]::OrdinalIgnoreCase)
    $plainCommandMatches = $installedCommand.StartsWith("$expectedPath ", [StringComparison]::OrdinalIgnoreCase)
    if (-not $quotedCommandMatches -and -not $plainCommandMatches) {
        if ($serviceName -eq 'Pixels.Setup') {
            $setupExecutable = Join-Path $resolvedInstall 'setup_payload/bin/px_console_admin.exe'
            if ($installedCommand.StartsWith("`"$setupExecutable`" ", [StringComparison]::OrdinalIgnoreCase)) {
                $quotedCommandMatches = $true
            }
        }
    }
    if (-not $quotedCommandMatches -and -not $plainCommandMatches) {
        if ($serviceName -like 'Pixels.Backup.*') { continue }
        throw "Service $serviceName does not belong to this install root."
    }
    if ($service.Status -ne 'Stopped') {
        & sc.exe stop $serviceName 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Cannot stop $serviceName" }
        $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
    }
    & sc.exe delete $serviceName 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Cannot remove $serviceName" }
}
if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($currentPath)) -cne $resolvedInstall) {
    throw 'Refusing cleanup outside install root.'
}
if (((Get-Item -LiteralPath $currentPath).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Refusing cleanup of a reparse point.'
}
Remove-Item -LiteralPath $currentPath -Recurse -Force
if ($currentPath -ne (Join-Path $resolvedInstall 'setup_payload')) {
    $setupPayload = Join-Path $resolvedInstall 'setup_payload'
    if (Test-Path -LiteralPath $setupPayload) {
        if (((Get-Item -LiteralPath $setupPayload).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Refusing cleanup of a setup payload reparse point.'
        }
        Remove-Item -LiteralPath $setupPayload -Recurse -Force
    }
}
foreach ($releaseDirectory in Get-ChildItem -LiteralPath $resolvedInstall -Directory) {
    if ($releaseDirectory.Name -match '^(?:previous|stage)-[0-9a-f]{32}$' -and
        [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($releaseDirectory.FullName)) -ceq $resolvedInstall -and
        ($releaseDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        Remove-Item -LiteralPath $releaseDirectory.FullName -Recurse -Force
    }
}
$deploymentMarker = Join-Path $resolvedInstall 'deployment.id'
if (Test-Path -LiteralPath $deploymentMarker -PathType Leaf) {
    Remove-Item -LiteralPath $deploymentMarker
}
Write-Output 'Pixels Server services and program files removed. Configuration, PostgreSQL and backup data were retained.'
