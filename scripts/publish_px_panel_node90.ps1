#requires -Version 7.0

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repository 'build_official/dist/px_panel.exe'
$machineFile = Join-Path $repository '.env/test_machine.md'
$targetHost = '39.71.45.66'
$target = 'D:\software\esprit_169811\render\px_panel.exe'
$staged = 'D:\software\esprit_169811\render\px_panel.staged.exe'

if (-not (Test-Path -LiteralPath $source)) {
    throw "Panel artifact is missing: $source"
}

$machineText = Get-Content -LiteralPath $machineFile -Raw
$password = [regex]::Match($machineText, '(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
if (-not $password) {
    throw 'Node90 password is missing from the test-machine document.'
}

$expectedHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$userName = if ($machineName) { "$machineName\Administrator" } else { 'Administrator' }
$credential = [pscredential]::new($userName, (ConvertTo-SecureString $password -AsPlainText -Force))
$previousTrustedHosts = (Get-Item WSMan:\localhost\Client\TrustedHosts).Value
$session = $null
try {
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $targetHost -Force
    $session = New-PSSession -ComputerName $targetHost -Credential $credential
    Copy-Item -LiteralPath $source -Destination $staged -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList $expectedHash, $target, $staged -ScriptBlock {
        param($expectedHash, $target, $staged)

        if ((Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'Staged Panel hash mismatch.'
        }

        Stop-ScheduledTask -TaskName 'px_panel_start' -ErrorAction SilentlyContinue
        Get-Process px_panel -ErrorAction SilentlyContinue | Stop-Process -Force
        $stopDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while ((Get-Process px_panel -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $stopDeadline) {
            Start-Sleep -Milliseconds 100
        }
        if (Get-Process px_panel -ErrorAction SilentlyContinue) {
            throw 'Remote Panel did not stop before publishing.'
        }
        Copy-Item -LiteralPath $staged -Destination $target -Force
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'Published Panel hash mismatch.'
        }
        Remove-Item -LiteralPath $staged -Force

        Start-ScheduledTask -TaskName 'px_panel_start'
        Start-Sleep -Seconds 10
        $process = Get-Process px_panel -ErrorAction Stop | Where-Object { $_.Path -eq $target } | Select-Object -First 1
        if (-not $process) {
            throw 'Deployed Panel did not start.'
        }

        [pscustomobject]@{
            Hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
            Responding = $process.Responding
            QtModuleCount = @($process.Modules | Where-Object { $_.ModuleName -match '^Qt\d' }).Count
            TaskState = (Get-ScheduledTask -TaskName 'px_panel_start').State
        }
    }
} finally {
    if ($session) {
        Remove-PSSession $session
    }
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $previousTrustedHosts -Force
}
