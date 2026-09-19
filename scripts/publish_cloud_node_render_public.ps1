#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$machineFile = Join-Path $repository '.env/test_machine.md'
$installDirectory = 'C:\Program Files\Pixels Cloud Node'
$artifacts = @(
    [pscustomobject]@{
        Name = 'px_render.exe'
        SourcePath = Join-Path $repository 'build_official/cloud_node/dist/px_render.exe'
        TargetPath = Join-Path $installDirectory 'px_render.exe'
        StagedPath = Join-Path $installDirectory 'px_render.staged.exe'
    },
    [pscustomobject]@{
        Name = 'px_render_rtc.dll'
        SourcePath = Join-Path $repository 'build_official/cloud_node/dist/px_render_rtc.dll'
        TargetPath = Join-Path $installDirectory 'px_render_rtc.dll'
        StagedPath = Join-Path $installDirectory 'px_render_rtc.staged.dll'
    }
)
foreach ($artifact in $artifacts) {
    if (-not (Test-Path -LiteralPath $artifact.SourcePath -PathType Leaf)) {
        throw "Render artifact is missing: $($artifact.SourcePath)"
    }
    $artifact | Add-Member -NotePropertyName ExpectedHash -NotePropertyValue (
        (Get-FileHash -LiteralPath $artifact.SourcePath -Algorithm SHA256).Hash)
}

$machineText = Get-Content -LiteralPath $machineFile -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) {
    throw 'Public test host machine-qualified credential is incomplete.'
}

$credential = [pscredential]::new(
    "$machineName\Administrator",
    (ConvertTo-SecureString $password -AsPlainText -Force)
)
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$session = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $session = New-PSSession -ComputerName $ComputerName -Credential $credential
    foreach ($artifact in $artifacts) {
        Copy-Item -LiteralPath $artifact.SourcePath -Destination $artifact.StagedPath -ToSession $session -Force
    }
    $renderHash = $artifacts[0].ExpectedHash
    $renderRtcHash = $artifacts[1].ExpectedHash
    Invoke-Command -Session $session -ArgumentList $renderHash, $renderRtcHash, $installDirectory -ScriptBlock {
        param($renderHash, $renderRtcHash, $installDirectory)

        $ErrorActionPreference = 'Stop'
        $artifacts = @(
            [pscustomobject]@{
                Name = 'px_render.exe'
                TargetPath = Join-Path $installDirectory 'px_render.exe'
                StagedPath = Join-Path $installDirectory 'px_render.staged.exe'
                ExpectedHash = $renderHash
            },
            [pscustomobject]@{
                Name = 'px_render_rtc.dll'
                TargetPath = Join-Path $installDirectory 'px_render_rtc.dll'
                StagedPath = Join-Path $installDirectory 'px_render_rtc.staged.dll'
                ExpectedHash = $renderRtcHash
            }
        )
        foreach ($artifact in $artifacts) {
            if ((Get-FileHash -LiteralPath $artifact.StagedPath -Algorithm SHA256).Hash -ne $artifact.ExpectedHash) {
                throw "Staged Render artifact hash mismatch: $($artifact.Name)"
            }
        }

        $backupDirectory = Join-Path $installDirectory ('render-runtime-before-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
        [void](New-Item -ItemType Directory -Path $backupDirectory)
        $serviceWasRunning = (Get-Service -Name 'px_service').Status -eq [ServiceProcess.ServiceControllerStatus]::Running
        try {
            if ($serviceWasRunning) {
                Stop-Service -Name 'px_service' -Force
                (Get-Service -Name 'px_service').WaitForStatus(
                    [ServiceProcess.ServiceControllerStatus]::Stopped,
                    [TimeSpan]::FromSeconds(20))
            }
            foreach ($process in @(Get-CimInstance Win32_Process | Where-Object {
                $_.Name -eq 'px_render.exe' -and $_.ExecutablePath -eq (Join-Path $installDirectory 'px_render.exe')
            })) {
                Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
            }
            foreach ($artifact in $artifacts) {
                Copy-Item -LiteralPath $artifact.TargetPath -Destination (Join-Path $backupDirectory $artifact.Name) -Force
                Copy-Item -LiteralPath $artifact.StagedPath -Destination $artifact.TargetPath -Force
                if ((Get-FileHash -LiteralPath $artifact.TargetPath -Algorithm SHA256).Hash -ne $artifact.ExpectedHash) {
                    throw "Installed Render artifact hash mismatch: $($artifact.Name)"
                }
            }
        }
        catch {
            foreach ($artifact in $artifacts) {
                $backupPath = Join-Path $backupDirectory $artifact.Name
                if (Test-Path -LiteralPath $backupPath -PathType Leaf) {
                    Copy-Item -LiteralPath $backupPath -Destination $artifact.TargetPath -Force
                }
            }
            throw
        }
        finally {
            foreach ($artifact in $artifacts) {
                Remove-Item -LiteralPath $artifact.StagedPath -Force -ErrorAction SilentlyContinue
            }
            if ($serviceWasRunning -and (Get-Service -Name 'px_service').Status -ne [ServiceProcess.ServiceControllerStatus]::Running) {
                Start-Service -Name 'px_service'
                (Get-Service -Name 'px_service').WaitForStatus(
                    [ServiceProcess.ServiceControllerStatus]::Running,
                    [TimeSpan]::FromSeconds(20))
            }
        }

        [pscustomobject]@{
            Artifacts = @($artifacts | ForEach-Object {
                [pscustomobject]@{
                    Name = $_.Name
                    Hash = (Get-FileHash -LiteralPath $_.TargetPath -Algorithm SHA256).Hash
                }
            })
            ServiceState = (Get-Service -Name 'px_service').Status.ToString()
            RecoverableBackup = $backupDirectory
        }
    }
}
finally {
    if ($session) {
        Remove-PSSession $session
    }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
