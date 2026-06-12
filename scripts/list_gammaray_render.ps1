# Lists all GammaRay.exe processes with full command-line arguments.
# Usage:
#   .\list_gammaray_processes.ps1
#   .\list_gammaray_processes.ps1 -Name "GammaRayRender.exe"

param(
    [string]$Name = "GammaRayRender.exe"
)

$filter = "Name = '$Name'"
$procs = @(Get-CimInstance Win32_Process -Filter $filter |
    Select-Object ProcessId,
                  Name,
                  ExecutablePath,
                  CommandLine,
                  ParentProcessId,
                  @{Name="StartTime"; Expression={
                      if ($_.CreationDate) {
                          $_.CreationDate.ToString("yyyy-MM-dd HH:mm:ss")
                      } else { "N/A" }
                  }})

if (-not $procs) {
    Write-Host "No $Name processes found." -ForegroundColor Yellow
    exit 0
}

Write-Host "Found $($procs.Count) $Name process(es):" -ForegroundColor Green
Write-Host ("=" * 80)

foreach ($p in $procs) {
    Write-Host "PID              : $($p.ProcessId)"
    Write-Host "Name             : $($p.Name)"
    Write-Host "Parent PID       : $($p.ParentProcessId)"
    Write-Host "Executable Path  : $($p.ExecutablePath)"
    Write-Host "Start Time       : $($p.StartTime)"
    Write-Host "Command Line     : $($p.CommandLine)"
    Write-Host ("-" * 80)
}
