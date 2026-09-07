param(
    [ValidateSet("Accept", "Reject")]
    [string]$Action,
    [ValidateRange(1, 60)]
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"
$probe = Join-Path $PSScriptRoot "remote_panel_consent_probe.ps1"
$output = Join-Path $PSScriptRoot ("consent_" + $Action.ToLowerInvariant())
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$lastError = $null
do {
    try {
        & $probe -Action $Action -OutputDirectory $output
        return
    } catch {
        $lastError = $_.Exception.Message
    }
    Start-Sleep -Milliseconds 250
} while ((Get-Date) -lt $deadline)

throw "Voice consent dialog did not become actionable: $lastError"
