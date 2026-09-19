#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repository 'build_official/cloud_node/dist/px_render.exe'
$machineFile = Join-Path $repository '.env/test_machine.md'
$installDirectory = 'C:\Program Files\Pixels Cloud Node'
$target = Join-Path $installDirectory 'px_render.exe'
$staged = Join-Path $installDirectory 'px_render.staged.exe'

if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Render artifact is missing: $source"
}

$machineText = Get-Content -LiteralPath $machineFile -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) {
    throw 'Public test host machine-qualified credential is incomplete.'
}

$expectedHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
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
    Copy-Item -LiteralPath $source -Destination $staged -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList $expectedHash, $target, $staged, $installDirectory -ScriptBlock {
        param($expectedHash, $target, $staged, $installDirectory)

        $ErrorActionPreference = 'Stop'
        if ((Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'Staged Render hash mismatch.'
        }

        $backup = Join-Path $installDirectory ('px_render.before-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss') + '.exe')
        $serviceWasRunning = (Get-Service -Name 'px_service').Status -eq [ServiceProcess.ServiceControllerStatus]::Running
        try {
            if ($serviceWasRunning) {
                Stop-Service -Name 'px_service' -Force
                (Get-Service -Name 'px_service').WaitForStatus(
                    [ServiceProcess.ServiceControllerStatus]::Stopped,
                    [TimeSpan]::FromSeconds(20))
            }
            foreach ($process in @(Get-CimInstance Win32_Process | Where-Object {
                $_.Name -eq 'px_render.exe' -and $_.ExecutablePath -eq $target
            })) {
                Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
            }
            Copy-Item -LiteralPath $target -Destination $backup -Force
            Copy-Item -LiteralPath $staged -Destination $target -Force
            if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expectedHash) {
                throw 'Installed Render hash mismatch.'
            }
        }
        catch {
            if (Test-Path -LiteralPath $backup -PathType Leaf) {
                Copy-Item -LiteralPath $backup -Destination $target -Force
            }
            throw
        }
        finally {
            Remove-Item -LiteralPath $staged -Force -ErrorAction SilentlyContinue
            if ($serviceWasRunning -and (Get-Service -Name 'px_service').Status -ne [ServiceProcess.ServiceControllerStatus]::Running) {
                Start-Service -Name 'px_service'
                (Get-Service -Name 'px_service').WaitForStatus(
                    [ServiceProcess.ServiceControllerStatus]::Running,
                    [TimeSpan]::FromSeconds(20))
            }
        }

        [pscustomobject]@{
            Hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
            ServiceState = (Get-Service -Name 'px_service').Status.ToString()
            RecoverableBackup = $backup
        }
    }
}
finally {
    if ($session) {
        Remove-PSSession $session
    }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
