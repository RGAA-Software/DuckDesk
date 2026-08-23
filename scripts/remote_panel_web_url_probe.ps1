param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$sessionId = (Get-Process -Id $PID).SessionId
$panels = @(
    Get-Process -Name "px_panel" -ErrorAction SilentlyContinue |
        Where-Object { $_.SessionId -eq $sessionId }
)
if ($panels.Count -ne 1) {
    throw "Expected exactly one px_panel in the current interactive session"
}

$condition = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
    $panels[0].Id)
$windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children,
    $condition)

$webUrl = ""
foreach ($window in $windows) {
    $elements = $window.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($element in $elements) {
        $candidates = @($element.Current.Name, $element.Current.HelpText)
        try {
            $pattern = $element.GetCurrentPattern(
                [System.Windows.Automation.ValuePattern]::Pattern)
            $candidates += $pattern.Current.Value
        } catch {
            # Not every UI Automation element exposes ValuePattern.
        }
        foreach ($candidate in $candidates) {
            if ($candidate -match "^https?://[^ ]+/web_client/\?c=") {
                $webUrl = $candidate
                break
            }
        }
        if ($webUrl) { break }
    }
    if ($webUrl) { break }
}

if (-not $webUrl) {
    throw "The Panel Web Client URL was not exposed through UI Automation"
}
[System.IO.File]::WriteAllText(
    $OutputPath, $webUrl, [System.Text.UTF8Encoding]::new($false))
[ordered]@{ ok = $true; session_id = $sessionId; panel_pid = $panels[0].Id } |
    ConvertTo-Json -Compress
