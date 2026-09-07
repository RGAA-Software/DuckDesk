param(
    [ValidateRange(1, 120)]
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$root = [System.Windows.Automation.AutomationElement]::RootElement
$buttons = [System.Windows.Automation.ControlType]::Button
$buttonCondition = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    $buttons
)

$lastDiagnostic = [DateTime]::MinValue
Write-Output "VOICE_CONSENT_HELPER_STARTED session=$((Get-Process -Id $PID).SessionId)"
while ([DateTime]::UtcNow -lt $deadline) {
    $panel = Get-Process -Name px_panel -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $panel) {
        $processCondition = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
            $panel.Id
        )
        $candidateCondition = New-Object System.Windows.Automation.AndCondition(
            $processCondition,
            $buttonCondition
        )
        $candidates = $root.FindAll(
            [System.Windows.Automation.TreeScope]::Descendants,
            $candidateCondition
        )
        foreach ($candidate in $candidates) {
            $automationId = $candidate.Current.AutomationId
            $helpText = $candidate.Current.HelpText
            $name = $candidate.Current.Name
            if ($automationId -notlike '*voice_call_accept' -and
                $helpText -ne 'px_voice_call_accept_v1' -and
                $name -notmatch '(?i)accept|allow|answer|同意|接受|允许|接听') {
                continue
            }
            $pattern = $candidate.GetCurrentPattern(
                [System.Windows.Automation.InvokePattern]::Pattern
            )
            $pattern.Invoke()
            Write-Output 'VOICE_CONSENT_ACCEPTED'
            exit 0
        }
        if ([DateTime]::UtcNow.Subtract($lastDiagnostic).TotalSeconds -ge 5) {
            $lastDiagnostic = [DateTime]::UtcNow
            $summary = foreach ($candidate in $candidates) {
                "id='$($candidate.Current.AutomationId)' name='$($candidate.Current.Name)' help='$($candidate.Current.HelpText)'"
            }
            Write-Output "VOICE_CONSENT_UIA panel_session=$($panel.SessionId) buttons=[$($summary -join '; ')]"
        }
    }
    Start-Sleep -Milliseconds 200
}

Write-Error 'Voice consent dialog was not found before timeout.'
exit 1
