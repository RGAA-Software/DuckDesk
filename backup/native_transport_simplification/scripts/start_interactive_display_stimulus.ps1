param(
    [string]$StimulusScript = "C:\Windows\Temp\display_capture_stimulus.ps1",
    [string]$TaskName = "GammaRay_Display_Capture_Stimulus",
    [int]$LifetimeSeconds = 300,

    # Optional compatibility parameters allow reuse of a pre-authorized SYSTEM
    # task entry point on remote test machines.
    [string]$Mode,
    [string]$Executable,
    [Alias("OutputPath")]
    [string]$ResultPath
)

$ErrorActionPreference = "Stop"

$interactiveUser = (Get-CimInstance Win32_ComputerSystem).UserName
if ([string]::IsNullOrWhiteSpace($interactiveUser)) {
    throw "No interactive Windows user is logged on"
}
if (-not (Test-Path -LiteralPath $StimulusScript)) {
    throw "Stimulus script is missing: $StimulusScript"
}

$arguments = "-NoLogo -NoProfile -ExecutionPolicy Bypass -File `"$StimulusScript`" -LifetimeSeconds $LifetimeSeconds"
$action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $arguments
$principal = New-ScheduledTaskPrincipal `
    -UserId $interactiveUser `
    -LogonType Interactive `
    -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 10)

Register-ScheduledTask `
    -TaskName $TaskName `
    -Action $action `
    -Principal $principal `
    -Settings $settings `
    -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName

if (-not [string]::IsNullOrWhiteSpace($ResultPath)) {
    [System.IO.File]::WriteAllText(
        $ResultPath,
        ([ordered]@{
            started_at = (Get-Date).ToString("o")
            interactive_user = $interactiveUser
            task_name = $TaskName
            lifetime_seconds = $LifetimeSeconds
        } | ConvertTo-Json),
        [System.Text.UTF8Encoding]::new($false)
    )
}
