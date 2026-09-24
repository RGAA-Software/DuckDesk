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
$supervisorPath = Join-Path $PSScriptRoot 'run_public_console_supervised.ps1'
$preserveLogsPath = Join-Path $PSScriptRoot 'preserve_public_console_logs.ps1'
foreach ($requiredPath in @($sourcePath, $machinePath, $supervisorPath, $preserveLogsPath)) {
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
    Copy-Item -LiteralPath $supervisorPath -Destination 'D:\PixelsServer\config\run-console-supervised.stage.ps1' -ToSession $session -Force
    Copy-Item -LiteralPath $preserveLogsPath -Destination 'D:\PixelsServer\config\preserve-console-logs.stage.ps1' -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList @(
        (Get-FileHash -LiteralPath $supervisorPath -Algorithm SHA256).Hash,
        (Get-FileHash -LiteralPath $preserveLogsPath -Algorithm SHA256).Hash
    ) -ScriptBlock {
        param($supervisorHash, $preserveLogsHash)

        $configDirectory = 'D:\PixelsServer\config'
        $launcherPath = Join-Path $configDirectory 'start-console.ps1'
        $supervisorStagePath = Join-Path $configDirectory 'run-console-supervised.stage.ps1'
        $supervisorTargetPath = Join-Path $configDirectory 'run-console-supervised.ps1'
        $preserveStagePath = Join-Path $configDirectory 'preserve-console-logs.stage.ps1'
        $preserveTargetPath = Join-Path $configDirectory 'preserve-console-logs.ps1'
        $stagedScripts = @(
            @{ path = $supervisorStagePath; hash = $supervisorHash },
            @{ path = $preserveStagePath; hash = $preserveLogsHash }
        )
        foreach ($stagedScript in $stagedScripts) {
            if ((Get-FileHash -LiteralPath $stagedScript.path -Algorithm SHA256).Hash -ne $stagedScript.hash) {
                throw "Console supervision staged file hash mismatch: $($stagedScript.path)"
            }
            $parseTokens = $null
            $parseErrors = $null
            [void][System.Management.Automation.Language.Parser]::ParseFile($stagedScript.path, [ref]$parseTokens, [ref]$parseErrors)
            if ($parseErrors) {
                throw "Console supervision staged file has invalid PowerShell syntax: $($stagedScript.path)"
            }
        }
        if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) {
            throw 'Console launcher is missing.'
        }
        $launcherText = Get-Content -LiteralPath $launcherPath -Raw
        $supervisorCall = '& "$serverRoot\config\run-console-supervised.ps1" -ServerRoot $serverRoot'
        if (-not $launcherText.Contains($supervisorCall)) {
            $launcherTailCandidates = [regex]::Matches(
                $launcherText,
                '(?m)^\s*(?:& "\$serverRoot\\config\\preserve-console-logs\.ps1"|while \(\$true\) \{)'
            )
            if ($launcherTailCandidates.Count -eq 0) {
                throw 'Console launcher tail is not a known unsupervised or supervised form.'
            }
            $launcherTail = $launcherTailCandidates[$launcherTailCandidates.Count - 1]
            $updatedLauncher = $launcherText.Substring(0, $launcherTail.Index).TrimEnd() + "`r`n`r`n" + $supervisorCall + "`r`n"
            $parseTokens = $null
            $parseErrors = $null
            [void][System.Management.Automation.Language.Parser]::ParseInput($updatedLauncher, [ref]$parseTokens, [ref]$parseErrors)
            if ($parseErrors) {
                throw 'Updated Console launcher has invalid PowerShell syntax.'
            }
            $backupPath = Join-Path $configDirectory ('start-console.before-supervision-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss') + '.ps1')
            Copy-Item -LiteralPath $launcherPath -Destination $backupPath
            [IO.File]::WriteAllText($launcherPath, $updatedLauncher, [Text.UTF8Encoding]::new($false))
        }
        Copy-Item -LiteralPath $supervisorStagePath -Destination $supervisorTargetPath -Force
        Copy-Item -LiteralPath $preserveStagePath -Destination $preserveTargetPath -Force
        if ((Get-FileHash -LiteralPath $supervisorTargetPath -Algorithm SHA256).Hash -ne $supervisorHash -or
            (Get-FileHash -LiteralPath $preserveTargetPath -Algorithm SHA256).Hash -ne $preserveLogsHash) {
            throw 'Installed Console supervision file hash mismatch.'
        }
        Remove-Item -LiteralPath $supervisorStagePath, $preserveStagePath -Force
    }
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
