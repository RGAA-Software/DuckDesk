#requires -Version 5.1

[CmdletBinding()]
param([string]$ComputerName = '39.71.45.66')

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$certificateDirectory = Join-Path $repositoryRoot '.env/pixels-single-server-tls-20260926-c4fdfa68-v3'
$certificateFiles = @{
    'ca.pem' = 'console-ca.crt'
    'server.crt' = 'console-tls.crt'
    'server.key' = 'console-tls.key'
}
$fileHashes = @{}
foreach ($sourceName in $certificateFiles.Keys) {
    $sourcePath = Join-Path $certificateDirectory $sourceName
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "TLS input missing: $sourceName" }
    $fileHashes[$sourceName] = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
}
$machineText = Get-Content -LiteralPath (Join-Path $repositoryRoot '.env/test_machine.md') -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) { throw 'Public test host credential is incomplete.' }
$credential = [pscredential]::new("$machineName\Administrator", (ConvertTo-SecureString $password -AsPlainText -Force))
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$remoteSession = $null
$stageName = 'tls-stage-' + [guid]::NewGuid().ToString('N')
$configRoot = 'C:\ProgramData\Pixels\Server\config'
$stageDirectory = Join-Path $configRoot $stageName
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $remoteSession = New-PSSession -ComputerName $ComputerName -Credential $credential
    Invoke-Command -Session $remoteSession -ArgumentList $configRoot, $stageDirectory -ScriptBlock {
        param($configurationRoot, $stagingDirectory)
        $environmentText = Get-Content -LiteralPath (Join-Path $configurationRoot 'console.env') -Raw
        if ($environmentText -notmatch '(?m)^PIXELS_DEPLOYMENT_ID=c4fdfa68-d9d4-4e15-9883-344bd84c415c\s*$' -or
            -not (Test-Path -LiteralPath (Join-Path $configurationRoot 'setup.complete')) -or
            (Test-Path -LiteralPath $stagingDirectory) -or
            (Get-Service -Name Pixels.Console).Status -ne 'Running' -or
            (Get-Service -Name Pixels.Relay).Status -ne 'Running') {
            throw 'Single Server TLS rotation preflight failed.'
        }
        New-Item -ItemType Directory -Path $stagingDirectory -ErrorAction Stop | Out-Null
    }
    foreach ($sourceName in $certificateFiles.Keys) {
        Copy-Item -LiteralPath (Join-Path $certificateDirectory $sourceName) `
            -Destination (Join-Path $stageDirectory $sourceName) -ToSession $remoteSession -ErrorAction Stop
    }
    Invoke-Command -Session $remoteSession -ArgumentList $configRoot, $stageDirectory, $fileHashes, $certificateFiles `
        -ScriptBlock {
            param($configurationRoot, $stagingDirectory, $expectedHashes, $fileMap)
            foreach ($sourceName in $fileMap.Keys) {
                $stagedFile = Join-Path $stagingDirectory $sourceName
                if ((Get-FileHash -LiteralPath $stagedFile -Algorithm SHA256).Hash -ne
                    $expectedHashes[$sourceName]) {
                    throw "Staged TLS certificate hash differs: $sourceName"
                }
            }
            $backupDirectory = Join-Path $configurationRoot `
                ('tls-before-rotation-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' +
                [guid]::NewGuid().ToString('N').Substring(0, 8))
            New-Item -ItemType Directory -Path $backupDirectory -ErrorAction Stop | Out-Null
            foreach ($sourceName in $fileMap.Keys) {
                $installedName = $fileMap[$sourceName]
                Copy-Item -LiteralPath (Join-Path $configurationRoot $installedName) `
                    -Destination (Join-Path $backupDirectory $installedName) -ErrorAction Stop
            }
            try {
                Stop-Service -Name Pixels.Relay -ErrorAction Stop
                Stop-Service -Name Pixels.Console -ErrorAction Stop
                foreach ($sourceName in $fileMap.Keys) {
                    $installedFile = Join-Path $configurationRoot $fileMap[$sourceName]
                    [IO.File]::WriteAllBytes($installedFile,
                        [IO.File]::ReadAllBytes((Join-Path $stagingDirectory $sourceName)))
                    if ((Get-FileHash -LiteralPath $installedFile -Algorithm SHA256).Hash -ne
                        $expectedHashes[$sourceName]) {
                        throw "Installed TLS certificate hash differs: $sourceName"
                    }
                }
                Start-Service -Name Pixels.Console -ErrorAction Stop
                (Get-Service -Name Pixels.Console).WaitForStatus('Running', [timespan]::FromSeconds(30))
                Start-Service -Name Pixels.Relay -ErrorAction Stop
                (Get-Service -Name Pixels.Relay).WaitForStatus('Running', [timespan]::FromSeconds(30))
            } catch {
                foreach ($sourceName in $fileMap.Keys) {
                    $installedName = $fileMap[$sourceName]
                    [IO.File]::WriteAllBytes((Join-Path $configurationRoot $installedName),
                        [IO.File]::ReadAllBytes((Join-Path $backupDirectory $installedName)))
                }
                Start-Service -Name Pixels.Console -ErrorAction SilentlyContinue
                Start-Service -Name Pixels.Relay -ErrorAction SilentlyContinue
                throw
            }
            [pscustomobject]@{
                result = 'TLS_ROTATED'
                previous_tls_backup = $backupDirectory
                console_service = (Get-Service -Name Pixels.Console).Status.ToString()
                relay_service = (Get-Service -Name Pixels.Relay).Status.ToString()
            } | ConvertTo-Json -Compress
        }
} finally {
    if ($remoteSession) {
        Invoke-Command -Session $remoteSession -ArgumentList $configRoot, $stageDirectory -ScriptBlock {
            param($configurationRoot, $stagingDirectory)
            $resolvedConfiguration = [IO.Path]::GetFullPath($configurationRoot).TrimEnd('\')
            $resolvedStage = [IO.Path]::GetFullPath($stagingDirectory)
            if ($resolvedStage.StartsWith($resolvedConfiguration + '\tls-stage-',
                    [StringComparison]::OrdinalIgnoreCase) -and
                (Test-Path -LiteralPath $resolvedStage -PathType Container)) {
                Remove-Item -LiteralPath $resolvedStage -Recurse -Force
            }
        }
        Remove-PSSession $remoteSession
    }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
