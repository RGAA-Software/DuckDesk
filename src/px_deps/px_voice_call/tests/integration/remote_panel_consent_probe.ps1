param(
    [ValidateSet("Probe", "Accept", "Reject")]
    [string]$Action = "Probe",
    [string]$OutputDirectory = "C:\px_stage\voice_consent_probe"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$panelProcesses = @(Get-Process -Name "px_panel" -ErrorAction SilentlyContinue |
    Where-Object { $_.SessionId -eq (Get-Process -Id $PID).SessionId })
if ($panelProcesses.Count -ne 1) {
    throw "Expected exactly one px_panel process in this interactive session; found $($panelProcesses.Count)."
}

$panelId = $panelProcesses[0].Id
$processCondition = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
    $panelId)
$windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children,
    $processCondition)

$dialogAutomationId = "voice_call_consent_dialog"
$dialogHelpText = "px_voice_call_consent_v1"
$acceptAutomationId = "voice_call_accept"
$rejectAutomationId = "voice_call_reject"
$incomingTitles = @("Incoming voice call", "收到语音通话请求", "收到語音通話請求")
$acceptNames = @("Accept", "接受")
$rejectNames = @("Reject", "拒绝", "拒絕")
$topLevelWindows = foreach ($window in $windows) {
    $rect = $window.Current.BoundingRectangle
    [ordered]@{
        name = $window.Current.Name
        class_name = $window.Current.ClassName
        automation_id = $window.Current.AutomationId
        help_text = $window.Current.HelpText
        enabled = $window.Current.IsEnabled
        offscreen = $window.Current.IsOffscreen
        bounds = [ordered]@{
            left = $rect.Left
            top = $rect.Top
            width = $rect.Width
            height = $rect.Height
        }
    }
}
$dialog = $null
foreach ($window in $windows) {
    if ($window.Current.AutomationId -eq $dialogAutomationId -or
        $window.Current.AutomationId -like "*.$dialogAutomationId" -or
        $window.Current.HelpText -eq $dialogHelpText -or
        $incomingTitles -contains $window.Current.Name) {
        $dialog = $window
        break
    }
    $windowRect = $window.Current.BoundingRectangle
    if ($window.Current.ClassName -eq "QDialog" -and
        [Math]::Abs($windowRect.Width - 480) -lt 2 -and
        [Math]::Abs($windowRect.Height - 300) -lt 2) {
        $dialog = $window
        break
    }
    # Some custom Qt title bars do not expose their visual title as the native
    # window name. Fall back to the distinctive Accept + Reject button pair.
    $descendants = $window.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.Condition]::TrueCondition)
    $names = @($descendants | ForEach-Object { $_.Current.Name })
    if (($names | Where-Object { $acceptNames -contains $_ }).Count -gt 0 -and
        ($names | Where-Object { $rejectNames -contains $_ }).Count -gt 0) {
        $dialog = $window
        break
    }
}
if (-not $dialog) {
    $failurePath = Join-Path $OutputDirectory "panel_windows_not_found.json"
    [System.IO.File]::WriteAllText(
        $failurePath,
        ([ordered]@{
            captured_at = [DateTimeOffset]::Now.ToString("o")
            session_id = (Get-Process -Id $PID).SessionId
            panel_pid = $panelId
            windows = @($topLevelWindows)
        } | ConvertTo-Json -Depth 8),
        [System.Text.UTF8Encoding]::new($false))
    throw "No visible voice-call consent dialog was found for px_panel PID $panelId."
}

$elements = $dialog.FindAll(
    [System.Windows.Automation.TreeScope]::Descendants,
    [System.Windows.Automation.Condition]::TrueCondition)
$controls = foreach ($element in $elements) {
    $rect = $element.Current.BoundingRectangle
    [ordered]@{
        name = $element.Current.Name
        automation_id = $element.Current.AutomationId
        help_text = $element.Current.HelpText
        control_type = $element.Current.ControlType.ProgrammaticName
        enabled = $element.Current.IsEnabled
        keyboard_focus = $element.Current.HasKeyboardFocus
        bounds = [ordered]@{
            left = $rect.Left
            top = $rect.Top
            width = $rect.Width
            height = $rect.Height
        }
    }
}

$dialogRect = $dialog.Current.BoundingRectangle
$result = [ordered]@{
    captured_at = [DateTimeOffset]::Now.ToString("o")
    session_id = (Get-Process -Id $PID).SessionId
    panel_pid = $panelId
    action = $Action
    dialog = [ordered]@{
        name = $dialog.Current.Name
        automation_id = $dialog.Current.AutomationId
        help_text = $dialog.Current.HelpText
        enabled = $dialog.Current.IsEnabled
        bounds = [ordered]@{
            left = $dialogRect.Left
            top = $dialogRect.Top
            width = $dialogRect.Width
            height = $dialogRect.Height
        }
    }
    controls = @($controls)
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss_fff"
$jsonPath = Join-Path $OutputDirectory "consent_$($Action.ToLowerInvariant())_$stamp.json"
[System.IO.File]::WriteAllText(
    $jsonPath,
    ($result | ConvertTo-Json -Depth 8),
    [System.Text.UTF8Encoding]::new($false))

$screenBounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bitmap = New-Object System.Drawing.Bitmap($screenBounds.Width, $screenBounds.Height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen(
        $screenBounds.Left,
        $screenBounds.Top,
        0,
        0,
        $bitmap.Size,
        [System.Drawing.CopyPixelOperation]::SourceCopy)
    $screenshotPath = Join-Path $OutputDirectory "consent_$($Action.ToLowerInvariant())_$stamp.png"
    $bitmap.Save($screenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

if ($Action -ne "Probe") {
    $buttonNames = if ($Action -eq "Accept") { $acceptNames } else { $rejectNames }
    $expectedAutomationId = if ($Action -eq "Accept") {
        $acceptAutomationId
    }
    else {
        $rejectAutomationId
    }
    $buttonCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Button)
    $buttons = $dialog.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        $buttonCondition)
    $target = $null
    foreach ($button in $buttons) {
        if ($button.Current.AutomationId -eq $expectedAutomationId -or
            $button.Current.AutomationId -like "*.$expectedAutomationId" -or
            $buttonNames -contains $button.Current.Name) {
            $target = $button
            break
        }
    }
    if (-not $target -and $buttons.Count -ge 2) {
        $orderedButtons = @($buttons | Sort-Object {
            $_.Current.BoundingRectangle.Left
        })
        $target = if ($Action -eq "Accept") {
            $orderedButtons[-1]
        }
        else {
            $orderedButtons[0]
        }
    }
    if (-not $target) {
        throw "The $Action button was not exposed through UI Automation."
    }
    $pattern = $target.GetCurrentPattern(
        [System.Windows.Automation.InvokePattern]::Pattern)
    $pattern.Invoke()
}

[ordered]@{
    ok = $true
    action = $Action
    json = $jsonPath
    screenshot = $screenshotPath
} | ConvertTo-Json -Compress
