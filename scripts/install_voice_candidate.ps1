param(
    [string]$InstallerPath = "",
    [Parameter(Mandatory = $true)]
    [ValidatePattern("^[0-9A-Fa-f]{64}$")]
    [string]$ExpectedSha256,
    [string]$ResultPath = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($InstallerPath)) {
    $installers = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter "Pixels_*_Setup.exe" -File)
    if ($installers.Count -ne 1) {
        throw "Expected exactly one Pixels setup executable beside this script"
    }
    $InstallerPath = $installers[0].FullName
}
if ([string]::IsNullOrWhiteSpace($ResultPath)) {
    $ResultPath = Join-Path $PSScriptRoot "install_result.json"
}
$startedAt = Get-Date
$actualSha256 = ""
$installerExitCode = -1
$errorText = $null

try {
    if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
        throw "Installer does not exist"
    }
    $actualSha256 = (Get-FileHash -LiteralPath $InstallerPath -Algorithm SHA256).Hash
    if ($actualSha256 -ne $ExpectedSha256.ToUpperInvariant()) {
        throw "Installer SHA-256 mismatch"
    }
    # Wait only for the NSIS process itself. PowerShell's Start-Process -Wait
    # may include the long-lived Panel descendant, while direct invocation of
    # a GUI executable does not reliably expose an exit code.
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $InstallerPath
    $startInfo.Arguments = "/S"
    $startInfo.UseShellExecute = $false
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $process.WaitForExit()
    $installerExitCode = $process.ExitCode
    if ($installerExitCode -ne 0) {
        throw "Silent installer returned a nonzero exit code"
    }
} catch {
    $errorText = $_.Exception.Message
}

$result = [ordered]@{
    passed = $null -eq $errorText -and $installerExitCode -eq 0
    started_at = $startedAt.ToString("o")
    finished_at = (Get-Date).ToString("o")
    computer_name = $env:COMPUTERNAME
    installer_path = $InstallerPath
    expected_sha256 = $ExpectedSha256.ToUpperInvariant()
    actual_sha256 = $actualSha256
    installer_exit_code = $installerExitCode
    error = $errorText
}
$result | ConvertTo-Json | Set-Content -LiteralPath $ResultPath -Encoding UTF8
$result | ConvertTo-Json
if ($result.passed) { exit 0 }
exit 1
