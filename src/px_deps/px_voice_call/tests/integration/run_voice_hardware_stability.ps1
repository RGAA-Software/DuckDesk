param(
    [Parameter(Mandatory = $true)]
    [string]$TestDirectory,
    [ValidateRange(2, 28800)]
    [int]$DurationSeconds = 7200,
    [string]$CaptureDeviceId = "",
    [string]$PlayoutDeviceId = ""
)

$ErrorActionPreference = "Stop"
$privacyPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone"
$privacyExisted = Test-Path -LiteralPath $privacyPath
$originalPrivacy = if ($privacyExisted) {
    (Get-ItemProperty -LiteralPath $privacyPath -Name Value -ErrorAction SilentlyContinue).Value
} else {
    $null
}
$startedAt = Get-Date
$exitCode = 1
$testError = $null
$testExecutable = Join-Path $TestDirectory "test_voice_call.exe"
$testExecutableHash = if (Test-Path -LiteralPath $testExecutable) {
    (Get-FileHash -LiteralPath $testExecutable -Algorithm SHA256).Hash
} else {
    $null
}

try {
    if (-not $privacyExisted) {
        New-Item -Path $privacyPath -Force | Out-Null
    }
    Set-ItemProperty -LiteralPath $privacyPath -Name Value -Value "Allow"

    $env:PX_TEST_VOICE_WASAPI = "1"
    $env:PX_TEST_VOICE_LONG_DURATION_SECONDS = [string]$DurationSeconds
    if ($CaptureDeviceId) {
        $env:PX_TEST_VOICE_CAPTURE_DEVICE_ID = $CaptureDeviceId
    }
    if ($PlayoutDeviceId) {
        $env:PX_TEST_VOICE_PLAYOUT_DEVICE_ID = $PlayoutDeviceId
    }

    Push-Location -LiteralPath $TestDirectory
    try {
        $arguments = @(
            "--gtest_filter=VoiceAudioEndpointTest.ConfigurableLongRunningStability"
            "--gtest_output=xml:voice_hardware_stability.xml"
        )
        & ".\test_voice_call.exe" @arguments *> ".\voice_hardware_stability.log"
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
} catch {
    $testError = $_.Exception.ToString()
    $exitCode = 1
} finally {
    Remove-Item Env:PX_TEST_VOICE_WASAPI -ErrorAction SilentlyContinue
    Remove-Item Env:PX_TEST_VOICE_LONG_DURATION_SECONDS -ErrorAction SilentlyContinue
    Remove-Item Env:PX_TEST_VOICE_CAPTURE_DEVICE_ID -ErrorAction SilentlyContinue
    Remove-Item Env:PX_TEST_VOICE_PLAYOUT_DEVICE_ID -ErrorAction SilentlyContinue

    if ($privacyExisted) {
        Set-ItemProperty -LiteralPath $privacyPath -Name Value -Value $originalPrivacy
    } else {
        Remove-Item -LiteralPath $privacyPath -Recurse -Force -ErrorAction SilentlyContinue
    }

    $result = [ordered]@{
        started_at = $startedAt.ToString("o")
        finished_at = (Get-Date).ToString("o")
        duration_seconds = $DurationSeconds
        computer_name = $env:COMPUTERNAME
        os_version = [Environment]::OSVersion.VersionString
        test_executable_sha256 = $testExecutableHash
        exit_code = $exitCode
        error = $testError
        original_privacy = $originalPrivacy
        restored_privacy = if (Test-Path -LiteralPath $privacyPath) {
            (Get-ItemProperty -LiteralPath $privacyPath -Name Value -ErrorAction SilentlyContinue).Value
        } else {
            $null
        }
    }
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $TestDirectory "voice_hardware_stability_result.json") -Encoding UTF8
}

exit $exitCode
