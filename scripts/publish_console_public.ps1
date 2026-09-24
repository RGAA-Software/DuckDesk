#requires -Version 7.0

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$sourcePath = Join-Path $repository '.cache/console-dev/release/px_console.exe'
$webDirectory = Join-Path $repository 'web/px_console/dist'
$machinePath = Join-Path $repository '.env/test_machine.md'
foreach ($requiredPath in @($sourcePath, $machinePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Public Console deployment input is missing: $requiredPath"
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $webDirectory 'index.html') -PathType Leaf)) {
    throw "Public Console web build is missing: $webDirectory"
}
$webFiles = @(Get-ChildItem -LiteralPath $webDirectory -File -Recurse | ForEach-Object {
    [pscustomobject]@{
        RelativePath = [IO.Path]::GetRelativePath($webDirectory, $_.FullName)
        SourcePath = $_.FullName
        Hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
})

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
$stageName = 'console-static.stage-' + [guid]::NewGuid().ToString('N')
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$session = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $session = New-PSSession -ComputerName $ComputerName -Credential $credential
    Copy-Item -LiteralPath $sourcePath -Destination 'D:\PixelsServer\app\bin\px_console.staged.exe' -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList $stageName -ScriptBlock {
        param($stageName)
        $stagePath = [IO.Path]::GetFullPath((Join-Path 'D:\PixelsServer\app' $stageName))
        $appDirectory = [IO.Path]::GetFullPath('D:\PixelsServer\app')
        if (-not $stagePath.StartsWith($appDirectory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Console static stage escaped its intended directory: $stagePath"
        }
        [void](New-Item -ItemType Directory -Path $stagePath)
    }
    foreach ($webFile in $webFiles) {
        $remoteRelativePath = $webFile.RelativePath.Replace('/', '\')
        $remoteFilePath = Join-Path (Join-Path 'D:\PixelsServer\app' $stageName) $remoteRelativePath
        $remoteParentPath = Split-Path $remoteFilePath -Parent
        Invoke-Command -Session $session -ArgumentList $remoteParentPath -ScriptBlock {
            param($parentPath)
            [void](New-Item -ItemType Directory -Path $parentPath -Force)
        }
        Copy-Item -LiteralPath $webFile.SourcePath -Destination $remoteFilePath -ToSession $session -Force
    }
    $webManifest = @($webFiles | Select-Object RelativePath, Hash)
    Invoke-Command -Session $session -ArgumentList $expectedHash, $stageName, $webManifest -ScriptBlock {
        param($expectedHash, $stageName, $webManifest)

        $ErrorActionPreference = 'Stop'
        $serverRoot = [IO.Path]::GetFullPath('D:\PixelsServer')
        $binaryDirectory = [IO.Path]::GetFullPath((Join-Path $serverRoot 'app\bin'))
        $targetPath = [IO.Path]::GetFullPath((Join-Path $binaryDirectory 'px_console.exe'))
        $stagedPath = [IO.Path]::GetFullPath((Join-Path $binaryDirectory 'px_console.staged.exe'))
        $diagnosticPath = [IO.Path]::GetFullPath((Join-Path $binaryDirectory 'px_console_admin.diag.exe'))
        $appDirectory = [IO.Path]::GetFullPath((Join-Path $serverRoot 'app'))
        $staticPath = [IO.Path]::GetFullPath((Join-Path $appDirectory 'console-static'))
        $staticStagePath = [IO.Path]::GetFullPath((Join-Path $appDirectory $stageName))
        foreach ($candidate in @($targetPath, $stagedPath, $diagnosticPath)) {
            if (-not $candidate.StartsWith($binaryDirectory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Console deployment path escaped its intended directory: $candidate"
            }
        }
        foreach ($candidate in @($staticPath, $staticStagePath)) {
            if (-not $candidate.StartsWith($appDirectory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Console static path escaped its intended directory: $candidate"
            }
        }
        if ((Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'Staged Console hash mismatch.'
        }
        $stagedFiles = @(Get-ChildItem -LiteralPath $staticStagePath -File -Recurse)
        if ($stagedFiles.Count -ne $webManifest.Count) {
            throw 'Staged Console web file count mismatch.'
        }
        foreach ($webFile in $webManifest) {
            $webPath = Join-Path $staticStagePath $webFile.RelativePath
            if (-not (Test-Path -LiteralPath $webPath -PathType Leaf) -or
                (Get-FileHash -LiteralPath $webPath -Algorithm SHA256).Hash -ne $webFile.Hash) {
                throw "Staged Console web hash mismatch: $($webFile.RelativePath)"
            }
        }

        $backupDirectory = Join-Path $serverRoot ('backups\console-before-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
        $staticBackupPath = Join-Path $backupDirectory 'console-static'
        $failedStaticPath = Join-Path $backupDirectory 'failed-console-static'
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

        $staticWasMoved = $false
        $newStaticWasInstalled = $false
        try {
            Stop-InstalledConsole
            Move-Item -LiteralPath $staticPath -Destination $staticBackupPath
            $staticWasMoved = $true
            Move-Item -LiteralPath $staticStagePath -Destination $staticPath
            $newStaticWasInstalled = $true
            Copy-Item -LiteralPath $stagedPath -Destination $targetPath -Force
            if ((Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $expectedHash) {
                throw 'Installed Console hash mismatch.'
            }
            if ($taskWasRunning) {
                Start-InstalledConsole
            }
            foreach ($webFile in $webManifest) {
                $webPath = Join-Path $staticPath $webFile.RelativePath
                if ((Get-FileHash -LiteralPath $webPath -Algorithm SHA256).Hash -ne $webFile.Hash) {
                    throw "Installed Console web hash mismatch: $($webFile.RelativePath)"
                }
            }
        } catch {
            $deploymentError = $_
            Stop-InstalledConsole
            Copy-Item -LiteralPath (Join-Path $backupDirectory 'px_console.exe') -Destination $targetPath -Force
            if ($newStaticWasInstalled) {
                Move-Item -LiteralPath $staticPath -Destination $failedStaticPath
            }
            if ($staticWasMoved) {
                Move-Item -LiteralPath $staticBackupPath -Destination $staticPath
            }
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
            WebFileCount = $webManifest.Count
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
