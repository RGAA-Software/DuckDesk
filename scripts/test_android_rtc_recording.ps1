param(
    [string]$Serial = ""
)

$ErrorActionPreference = "Stop"
$adbArguments = @()
if ($Serial) {
    $adbArguments += @("-s", $Serial)
}

function Invoke-Adb([string[]]$Arguments) {
    & adb @adbArguments @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed: $($Arguments -join ' ')"
    }
}

$packagePath = & adb @adbArguments shell pm path yun.pixels.client.debug
if ($LASTEXITCODE -ne 0 -or -not ($packagePath -match '^package:')) {
    throw "Pixels debug app is not installed on the selected device."
}

Invoke-Adb @("logcat", "-c")
Invoke-Adb @(
    "shell", "am", "broadcast",
    "-a", "yun.pixels.client.debug.RTC_RECORDING_SMOKE",
    "-n", "yun.pixels.client.debug/yun.pixels.client.debug.RtcRecordingSmokeReceiver"
)

$deadline = [DateTime]::UtcNow.AddSeconds(30)
do {
    $result = & adb @adbArguments logcat -d -v brief -s PixelsRtcRecordTest:I '*:S'
    if ($result -match 'PixelsRtcRecordTest.*PASS') {
        Write-Host $result
        exit 0
    }
    if ($result -match 'PixelsRtcRecordTest.*FAIL') {
        throw $result
    }
    Start-Sleep -Milliseconds 500
} while ([DateTime]::UtcNow -lt $deadline)

throw "RTC recording hardware smoke test timed out."
