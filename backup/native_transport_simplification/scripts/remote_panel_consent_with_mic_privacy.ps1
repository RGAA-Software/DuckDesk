param(
    [ValidateRange(5, 90)]
    [int]$HoldAfterActionSeconds = 30,
    [ValidateRange(1, 60)]
    [int]$DialogTimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"
$microphoneKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone"
$resultPath = Join-Path $PSScriptRoot "consent_accept_privacy_result.json"
$startedAt = Get-Date
$keyExisted = Test-Path -LiteralPath $microphoneKey
$propertyExisted = $false
$originalValue = $null
$result = [ordered]@{
    startedAt = $startedAt.ToString("o")
    action = "Accept"
    originalPrivacyValue = $null
    restoredPrivacyValue = $null
    result = "FAIL"
    error = $null
}

try {
    if ($keyExisted) {
        $property = Get-ItemProperty -LiteralPath $microphoneKey -Name Value -ErrorAction SilentlyContinue
        if ($null -ne $property) {
            $propertyExisted = $true
            $originalValue = [string]$property.Value
        }
    } else {
        New-Item -Path $microphoneKey -Force | Out-Null
    }
    $result.originalPrivacyValue = $originalValue
    Set-ItemProperty -LiteralPath $microphoneKey -Name Value -Value "Allow" -Type String

    & (Join-Path $PSScriptRoot "remote_panel_consent_wait.ps1") `
        -Action Accept -TimeoutSeconds $DialogTimeoutSeconds

    # Keep the policy enabled only while the browser validates bidirectional RTP
    # and local hang-up. The finally block restores it even when the test fails.
    Start-Sleep -Seconds $HoldAfterActionSeconds
    $result.result = "PASS"
} catch {
    $result.error = $_.Exception.Message
    throw
} finally {
    if ($propertyExisted) {
        Set-ItemProperty -LiteralPath $microphoneKey -Name Value -Value $originalValue -Type String
    } else {
        Remove-ItemProperty -LiteralPath $microphoneKey -Name Value -ErrorAction SilentlyContinue
        if (-not $keyExisted) {
            Remove-Item -LiteralPath $microphoneKey -ErrorAction SilentlyContinue
        }
    }
    $restored = Get-ItemProperty -LiteralPath $microphoneKey -Name Value -ErrorAction SilentlyContinue
    if ($null -ne $restored) {
        $result.restoredPrivacyValue = [string]$restored.Value
    }
    $result.finishedAt = (Get-Date).ToString("o")
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultPath -Encoding UTF8
}
