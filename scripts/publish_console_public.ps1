#requires -Version 7.0

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$sourcePath = Join-Path $repository '.cache/console-dev/release/px_console.exe'
$machinePath = Join-Path $repository '.env/test_machine.md'
foreach ($requiredPath in @($sourcePath, $machinePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Public Console deployment input is missing: $requiredPath"
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

$credential = [pscredential]::new(
    $qualifiedUsername,
    (ConvertTo-SecureString $password -AsPlainText -Force)
)
$expectedHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$session = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $session = New-PSSession -ComputerName $ComputerName -Credential $credential
    Copy-Item -LiteralPath $sourcePath -Destination 'D:\PixelsServer\app\bin\px_console.staged.exe' -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList $expectedHash -ScriptBlock {
        param($expectedHash)

        $ErrorActionPreference = 'Stop'
        $serverRoot = [IO.Path]::GetFullPath('D:\PixelsServer')
        $binaryDirectory = [IO.Path]::GetFullPath((Join-Path $serverRoot 'app\bin'))
        $targetPath = [IO.Path]::GetFullPath((Join-Path $binaryDirectory 'px_console.exe'))
        $stagedPath = [IO.Path]::GetFullPath((Join-Path $binaryDirectory 'px_console.staged.exe'))
        $diagnosticPath = [IO.Path]::GetFullPath((Join-Path $binaryDirectory 'px_console_admin.diag.exe'))
        foreach ($candidate in @($targetPath, $stagedPath, $diagnosticPath)) {
            if (-not $candidate.StartsWith($binaryDirectory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Console deployment path escaped its intended directory: $candidate"
            }
        }
        if ((Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'Staged Console hash mismatch.'
        }

        $backupDirectory = Join-Path $serverRoot ('backups\console-before-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
        [void](New-Item -ItemType Directory -Path $backupDirectory)
        Copy-Item -LiteralPath $targetPath -Destination (Join-Path $backupDirectory 'px_console.exe') -Force
        $taskWasRunning = (Get-ScheduledTask -TaskName 'Pixels-Console').State -eq 'Running' -or
            @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $targetPath }).Count -gt 0

        function Stop-InstalledConsole {
            Stop-ScheduledTask -TaskName 'Pixels-Console' -ErrorAction SilentlyContinue
            foreach ($consoleProcess in @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $targetPath })) {
                Stop-Process -Id $consoleProcess.ProcessId -Force
            }
            for ($attempt = 0; $attempt -lt 80; $attempt++) {
                $remainingProcesses = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $targetPath })
                if ($remainingProcesses.Count -eq 0) {
                    return
                }
                Start-Sleep -Milliseconds 250
            }
            throw 'Console process did not stop before replacement.'
        }

        function Start-InstalledConsole {
            Start-ScheduledTask -TaskName 'Pixels-Console'
            for ($attempt = 0; $attempt -lt 80; $attempt++) {
                Start-Sleep -Milliseconds 250
                if (Test-NetConnection -ComputerName '127.0.0.1' -Port 4600 -InformationLevel Quiet -WarningAction SilentlyContinue) {
                    Start-Sleep -Seconds 2
                    if ((Get-ScheduledTask -TaskName 'Pixels-Console').State -eq 'Running') {
                        return
                    }
                }
            }
            throw 'Console did not remain healthy after deployment.'
        }

        try {
            Stop-InstalledConsole
            Copy-Item -LiteralPath $stagedPath -Destination $targetPath -Force
            if ((Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $expectedHash) {
                throw 'Installed Console hash mismatch.'
            }
            if ($taskWasRunning) {
                Start-InstalledConsole
            }
        } catch {
            $deploymentError = $_
            Stop-InstalledConsole
            Copy-Item -LiteralPath (Join-Path $backupDirectory 'px_console.exe') -Destination $targetPath -Force
            if ($taskWasRunning) {
                Start-InstalledConsole
            }
            throw $deploymentError
        } finally {
            Remove-Item -LiteralPath $stagedPath -Force -ErrorAction SilentlyContinue
        }

        if (Test-Path -LiteralPath $diagnosticPath -PathType Leaf) {
            Remove-Item -LiteralPath $diagnosticPath -Force
        }
        [pscustomobject]@{
            Hash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
            TaskState = [string](Get-ScheduledTask -TaskName 'Pixels-Console').State
            RecoverableBackup = $backupDirectory
            DiagnosticRemoved = -not (Test-Path -LiteralPath $diagnosticPath)
        }
    }
} finally {
    if ($session) {
        Remove-PSSession $session
    }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
    $password = $null
}
