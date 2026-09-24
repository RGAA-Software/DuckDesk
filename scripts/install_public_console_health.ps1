#requires -Version 7.0

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$machinePath = Join-Path $repository '.env/test_machine.md'
$probePath = Join-Path $PSScriptRoot 'check_public_console_health.ps1'
foreach ($requiredPath in @($machinePath, $probePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Public Console health installation input is missing: $requiredPath"
    }
}

$machineText = Get-Content -LiteralPath $machinePath -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$username = [regex]::Match($machineText, '(?m)^\s*-\s*\u7528\u6237\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName -or -not $username) {
    throw 'Public test host machine-qualified credential is incomplete.'
}
$qualifiedUsername = if ($username.Contains('\')) { $username } else { "$machineName\$username" }
$credential = [pscredential]::new($qualifiedUsername, (ConvertTo-SecureString $password -AsPlainText -Force))
$expectedHash = (Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$session = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $session = New-PSSession -ComputerName $ComputerName -Credential $credential
    $stagePath = 'D:\PixelsServer\config\check-console-health.stage.ps1'
    Copy-Item -LiteralPath $probePath -Destination $stagePath -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList $expectedHash -ScriptBlock {
        param($expectedHash)

        $configDirectory = 'D:\PixelsServer\config'
        $stagePath = Join-Path $configDirectory 'check-console-health.stage.ps1'
        $targetPath = Join-Path $configDirectory 'check-console-health.ps1'
        $certificatePath = 'D:\PixelsServer\app\tls\console.crt'
        $taskName = 'Pixels-Console-Health'
        $eventSource = 'PixelsConsoleHealth'
        foreach ($requiredPath in @($stagePath, $certificatePath)) {
            if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
                throw "Console health probe prerequisite is missing: $requiredPath"
            }
        }
        if ((Get-FileHash -LiteralPath $stagePath -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'Staged Console health probe hash mismatch.'
        }
        $parseTokens = $null
        $parseErrors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile($stagePath, [ref]$parseTokens, [ref]$parseErrors)
        if ($parseErrors) {
            throw 'Staged Console health probe has invalid PowerShell syntax.'
        }
        $existingTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if ($existingTask -and @($existingTask.Actions | Where-Object { $_.Arguments -notlike "*$targetPath*" }).Count -gt 0) {
            throw 'Existing Console health task does not point to the expected probe.'
        }
        if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
            $backupPath = Join-Path $configDirectory ('check-console-health.before-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss') + '.ps1')
            Copy-Item -LiteralPath $targetPath -Destination $backupPath
        }
        Copy-Item -LiteralPath $stagePath -Destination $targetPath -Force
        Remove-Item -LiteralPath $stagePath -Force
        if ((Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'Installed Console health probe hash mismatch.'
        }
        if (-not [Diagnostics.EventLog]::SourceExists($eventSource)) {
            New-EventLog -LogName Application -Source $eventSource
        }
        $action = New-ScheduledTaskAction -Execute 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' `
            -Argument ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{0}"' -f $targetPath)
        $trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) -RepetitionInterval (New-TimeSpan -Minutes 1)
        $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
        $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds 30) -MultipleInstances IgnoreNew
        Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
        [pscustomobject]@{
            ProbeHash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
            EventSource = $eventSource
            TaskState = [string](Get-ScheduledTask -TaskName $taskName).State
            TaskInterval = (Get-ScheduledTask -TaskName $taskName).Triggers[0].Repetition.Interval
        }
    }
} finally {
    if ($session) {
        Remove-PSSession $session
    }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
    $password = $null
}
