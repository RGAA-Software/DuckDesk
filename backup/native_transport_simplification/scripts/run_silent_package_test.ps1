param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Install", "Uninstall")]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [Parameter(Mandatory = $true)]
    [string]$ResultPath
)

$ErrorActionPreference = "Stop"
$startedAt = Get-Date

try {
    # Start-Process -Wait also waits for the installer's descendant tree on
    # Windows. The product intentionally launches px_panel.exe after setup,
    # so use Process.WaitForExit() to wait for the NSIS process only.
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new($Executable, "/S")
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    $errorText = $null
} catch {
    $exitCode = 1603
    $errorText = $_.Exception.Message
}

$result = [ordered]@{
    mode = $Mode
    executable = $Executable
    started_at = $startedAt.ToString("o")
    finished_at = (Get-Date).ToString("o")
    exit_code = $exitCode
    error = $errorText
}

[System.IO.File]::WriteAllText(
    $ResultPath,
    ($result | ConvertTo-Json -Depth 3),
    [System.Text.UTF8Encoding]::new($false)
)

exit $exitCode
