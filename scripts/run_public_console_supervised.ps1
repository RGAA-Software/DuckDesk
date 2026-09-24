#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$ServerRoot = 'D:\PixelsServer'
)

$ErrorActionPreference = 'Stop'
$consolePath = Join-Path $ServerRoot 'app\bin\px_console.exe'
$stdoutPath = Join-Path $ServerRoot 'logs\console.stdout.log'
$stderrPath = Join-Path $ServerRoot 'logs\console.stderr.log'
$preserveLogsPath = Join-Path $ServerRoot 'config\preserve-console-logs.ps1'

while ($true) {
    try {
        & $preserveLogsPath -ServerRoot $ServerRoot
        $consoleProcess = Start-Process `
            -FilePath $consolePath `
            -WindowStyle Hidden `
            -PassThru `
            -Wait `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath
        if ($consoleProcess.ExitCode -eq 0) {
            exit 0
        }
        Write-Warning "Console exited with code $($consoleProcess.ExitCode); retrying in five seconds."
    } catch {
        Write-Warning "Console launch failed; retrying in five seconds: $($_.Exception.Message)"
    }
    Start-Sleep -Seconds 5
}
