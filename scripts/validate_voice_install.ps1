param(
    [string]$InstallDirectory = "C:\Program Files\PixelsRender",
    [string]$ExpectedVersion = "",
    [string]$OutputPath = "C:\Windows\Temp\GammaRayVoiceInstallValidation.json",
    [ValidateRange(0, 300)]
    [int]$WaitSeconds = 90
)

$ErrorActionPreference = "Stop"
trap {
    $failure = [ordered]@{
        passed = $false
        finished_at = (Get-Date).ToString("o")
        computer_name = $env:COMPUTERNAME
        fatal_error = $_.Exception.ToString()
    }
    $failure | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
    exit 2
}
$startedAt = Get-Date
$requiredFiles = @(
    "px_panel.exe",
    "px_render.exe",
    "px_service.exe",
    "px_client.exe",
    "px_voice_apm.dll",
    "deps\rd_plugins\voice_call.dll",
    "deps\rd_plugins\net_rtc_local.dll",
    "web_client\index.html",
    "usbmmidd_v2\deviceinstaller64.exe",
    "usbmmidd_v2\usbmmIdd.inf",
    "Uninstall.exe"
)

function Get-InstallState {
    $service = Get-Service -Name "px_service" -ErrorAction SilentlyContinue
    $panel = @(Get-Process -Name "px_panel" -ErrorAction SilentlyContinue)
    $interactiveSessions = @(
        Get-Process -Name "explorer" -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty SessionId -Unique
    )
    $driver = @(
        Get-PnpDevice -Class Display -ErrorAction SilentlyContinue |
            Where-Object { $_.FriendlyName -eq "USB Mobile Monitor Virtual Display" }
    )
    [ordered]@{
        service = $service
        panel = $panel
        interactive_sessions = $interactiveSessions
        driver = $driver
    }
}

$deadline = (Get-Date).AddSeconds($WaitSeconds)
do {
    $state = Get-InstallState
    if ($state.service.Status -eq "Running" -and
        @($state.panel | Where-Object { $state.interactive_sessions -contains $_.SessionId }).Count -gt 0 -and
        @($state.driver | Where-Object Status -eq "OK").Count -gt 0) {
        break
    }
    if ((Get-Date) -ge $deadline) {
        break
    }
    Start-Sleep -Seconds 2
} while ($true)

$missingFiles = @(
    $requiredFiles | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $InstallDirectory $_) -PathType Leaf)
    }
)
$hashes = [ordered]@{}
foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $InstallDirectory $relativePath
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $hashes[$relativePath] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
}

$languagePath = Join-Path $InstallDirectory "resources\language\english.json"
$language = if (Test-Path -LiteralPath $languagePath) {
    [System.IO.File]::ReadAllText($languagePath, [System.Text.Encoding]::UTF8) |
        ConvertFrom-Json
} else {
    $null
}
$initiatorWarningPresent = $null -ne $language -and
    $language.id_voice_call_headset_warning -match "Pause Remote Sound"
$controlledWarningPresent = $null -ne $language -and
    $language.id_voice_call_consent_warning -match "pause application audio on this computer before accepting"

$uninstall = $null
foreach ($uninstallKey in @(
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Pixels px_panel",
    "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Pixels px_panel"
)) {
    $uninstall = Get-ItemProperty -LiteralPath $uninstallKey -ErrorAction SilentlyContinue
    if ($null -ne $uninstall) { break }
}
$versionMatches = [string]::IsNullOrWhiteSpace($ExpectedVersion) -or
    $uninstall.DisplayVersion -eq $ExpectedVersion

$signedDrivers = @()
foreach ($device in $state.driver) {
    $signed = Get-CimInstance Win32_PnPSignedDriver -ErrorAction SilentlyContinue |
        Where-Object { $_.DeviceID -eq $device.InstanceId } |
        Select-Object -First 1
    $signedDrivers += [ordered]@{
        instance_id = $device.InstanceId
        status = [string]$device.Status
        inf_name = [string]$signed.InfName
        driver_version = [string]$signed.DriverVersion
        driver_provider = [string]$signed.DriverProviderName
        is_signed = [bool]$signed.IsSigned
    }
}

$productProcesses = @(
    Get-Process -Name "px_panel", "px_service", "px_render" -ErrorAction SilentlyContinue |
        Select-Object Name, Id, SessionId, StartTime, Responding
)
$processIds = @($productProcesses | ForEach-Object Id)
$listeningPorts = if ($processIds.Count -gt 0) {
    @(
        Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
            Where-Object { $processIds -contains $_.OwningProcess } |
            Select-Object LocalAddress, LocalPort, OwningProcess
    )
} else {
    @()
}

$checks = [ordered]@{
    required_files_present = $missingFiles.Count -eq 0
    service_running = $state.service.Status -eq "Running"
    panel_running = $state.panel.Count -gt 0
    panel_in_interactive_session = @(
        $state.panel | Where-Object { $state.interactive_sessions -contains $_.SessionId }
    ).Count -gt 0
    usbmmidd_healthy = @($state.driver | Where-Object Status -eq "OK").Count -gt 0
    uninstall_registered = $null -ne $uninstall
    expected_version_matches = $versionMatches
    initiator_warning_present = $initiatorWarningPresent
    controlled_warning_present = $controlledWarningPresent
}
$passed = @($checks.Values | Where-Object { -not $_ }).Count -eq 0

$result = [ordered]@{
    passed = $passed
    started_at = $startedAt.ToString("o")
    finished_at = (Get-Date).ToString("o")
    computer_name = $env:COMPUTERNAME
    install_directory = $InstallDirectory
    expected_version = $ExpectedVersion
    installed_version = [string]$uninstall.DisplayVersion
    checks = $checks
    missing_files = $missingFiles
    file_sha256 = $hashes
    service = if ($state.service) {
        [ordered]@{ name = $state.service.Name; status = [string]$state.service.Status }
    } else { $null }
    processes = $productProcesses
    interactive_session_ids = $state.interactive_sessions
    listening_ports = $listeningPorts
    display_drivers = $signedDrivers
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($passed) { exit 0 }
exit 1
