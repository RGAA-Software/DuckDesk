[CmdletBinding()]
param([switch]$ParentProcess)

$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { return }
$launcherProcessId = $PID
if ($ParentProcess) {
    $launcherProcessId = (Get-CimInstance Win32_Process -Filter "ProcessId=$PID").ParentProcessId
}
$launcherProcess = [Diagnostics.Process]::GetProcessById($launcherProcessId)
try {
    $allowedAffinity = $launcherProcess.ProcessorAffinity.ToInt64()
    $limitedAffinity = 0L
    $processorCount = 0
    for ($processorIndex = 0; $processorIndex -lt 64 -and $processorCount -lt 10; ++$processorIndex) {
        $processorMask = 1L -shl $processorIndex
        if (($allowedAffinity -band $processorMask) -ne 0) {
            $limitedAffinity = $limitedAffinity -bor $processorMask
            ++$processorCount
        }
    }
    if ($processorCount -eq 0) { throw 'No available processors for this build.' }
    $launcherProcess.ProcessorAffinity = [IntPtr]::new($limitedAffinity)
    Write-Host "Build CPU limit: launcher=$launcherProcessId logical_processors=$processorCount affinity=$limitedAffinity"
} finally {
    $launcherProcess.Dispose()
}
