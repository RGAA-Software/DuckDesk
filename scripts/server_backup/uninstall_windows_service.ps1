#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [Guid]$DeploymentId
)

$ErrorActionPreference = 'Stop'
$deploymentShortId = $DeploymentId.ToString('N').Substring(0, 12)
$serviceName = "Pixels.Backup.$deploymentShortId"
$service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($null -eq $service) {
    Write-Output "NOT_INSTALLED $serviceName"
    exit 0
}
if ($service.Status -ne 'Stopped') {
    $stopOutput = & sc.exe stop $serviceName 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to stop $serviceName`: $($stopOutput -join ' ')"
    }
    $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
}
$deleteOutput = & sc.exe delete $serviceName 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Unable to delete $serviceName`: $($deleteOutput -join ' ')"
}
Write-Output "REMOVED $serviceName; backup data was preserved"
