#requires -Version 7.0

[CmdletBinding()]
param(
    [switch]$ReplaceInvalidAllZeroRdpKey
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$sourceExe = Join-Path $repository 'output/px_console/px_console.exe'
$sourceConfig = Join-Path $repository 'px_console.toml'
$machineFile = Join-Path $repository '.env/test_machine.md'
$targetHost = '39.71.45.66'
$targetDirectory = 'D:\software\esprit_169811\console'

foreach ($source in @($sourceExe, $sourceConfig, $machineFile)) {
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Required deployment input is missing: $source"
    }
}

$machineText = Get-Content -LiteralPath $machineFile -Raw
$password = [regex]::Match($machineText, '(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
if (-not $password) {
    throw 'Node90 password is missing from the test-machine document.'
}

$expectedExeHash = (Get-FileHash -LiteralPath $sourceExe -Algorithm SHA256).Hash
$expectedConfigHash = (Get-FileHash -LiteralPath $sourceConfig -Algorithm SHA256).Hash
$credential = [pscredential]::new('administrator', (ConvertTo-SecureString $password -AsPlainText -Force))
$previousTrustedHosts = (Get-Item WSMan:\localhost\Client\TrustedHosts).Value
$session = $null
try {
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $targetHost -Force
    $session = New-PSSession -ComputerName $targetHost -Credential $credential
    $stagedExe = Join-Path $targetDirectory 'px_console.staged.exe'
    $stagedConfig = Join-Path $targetDirectory 'px_console.staged.toml'
    Copy-Item -LiteralPath $sourceExe -Destination $stagedExe -ToSession $session -Force
    Copy-Item -LiteralPath $sourceConfig -Destination $stagedConfig -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList $expectedExeHash, $expectedConfigHash, $targetDirectory, ([bool]$ReplaceInvalidAllZeroRdpKey) -ScriptBlock {
        param($exeHash, $configHash, $directory, $replaceInvalidAllZeroRdpKey)

        $targetExe = Join-Path $directory 'px_console.exe'
        $targetConfig = Join-Path $directory 'px_console.toml'
        $secretDirectory = Join-Path $directory 'secrets'
        $rdpMasterKey = Join-Path $secretDirectory 'rdp_workspace.key'
        $stagedExe = Join-Path $directory 'px_console.staged.exe'
        $stagedConfig = Join-Path $directory 'px_console.staged.toml'
        if ((Get-FileHash -LiteralPath $stagedExe -Algorithm SHA256).Hash -ne $exeHash -or
            (Get-FileHash -LiteralPath $stagedConfig -Algorithm SHA256).Hash -ne $configHash) {
            throw 'Console staging hash mismatch.'
        }

        Stop-ScheduledTask -TaskName 'Pixels-Console-Debug' -ErrorAction SilentlyContinue
        Get-Process px_console -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -eq $targetExe } |
            Stop-Process -Force
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        while ((Get-Process px_console -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 100
        }
        if (Get-Process px_console -ErrorAction SilentlyContinue) {
            throw 'Console did not stop before publishing.'
        }

        Copy-Item -LiteralPath $stagedExe -Destination $targetExe -Force
        Copy-Item -LiteralPath $stagedConfig -Destination $targetConfig -Force
        Remove-Item -LiteralPath $stagedExe, $stagedConfig -Force
        if ((Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash -ne $exeHash -or
            (Get-FileHash -LiteralPath $targetConfig -Algorithm SHA256).Hash -ne $configHash) {
            throw 'Console published hash mismatch.'
        }

        [void](New-Item -ItemType Directory -Path $secretDirectory -Force)
        if (Test-Path -LiteralPath $rdpMasterKey) {
            $existingKey = [IO.File]::ReadAllBytes($rdpMasterKey)
            try {
                $allZero = $existingKey.Length -eq 32 -and -not ($existingKey | Where-Object { $_ -ne 0 })
            } finally {
                [Array]::Clear($existingKey, 0, $existingKey.Length)
            }
            if ($allZero -and $replaceInvalidAllZeroRdpKey) {
                $resolvedRoot = [IO.Path]::GetFullPath($directory)
                $resolvedKey = [IO.Path]::GetFullPath($rdpMasterKey)
                if (-not $resolvedKey.StartsWith($resolvedRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
                    throw 'Refusing to replace an RDP key outside the Console installation directory.'
                }
                Remove-Item -LiteralPath $resolvedKey -Force
            }
        }
        if (-not (Test-Path -LiteralPath $rdpMasterKey)) {
            $key = [byte[]]::new(32)
            $random = [Security.Cryptography.RandomNumberGenerator]::Create()
            try {
                $random.GetBytes($key)
                [IO.File]::WriteAllBytes($rdpMasterKey, $key)
            } finally {
                $random.Dispose()
                [Array]::Clear($key, 0, $key.Length)
            }
        }
        if ((Get-Item -LiteralPath $rdpMasterKey).Length -ne 32) {
            throw 'The existing RDP workspace master key is invalid; refusing to replace it.'
        }
        $keyProbe = [IO.File]::ReadAllBytes($rdpMasterKey)
        try {
            if (-not ($keyProbe | Where-Object { $_ -ne 0 })) {
                throw 'The existing RDP workspace master key is all-zero; refusing to use it.'
            }
        } finally {
            [Array]::Clear($keyProbe, 0, $keyProbe.Length)
        }
        & icacls.exe $secretDirectory /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw 'Unable to restrict the RDP workspace key directory ACL.'
        }

        Start-ScheduledTask -TaskName 'Pixels-Console-Debug'
        Start-Sleep -Seconds 5
        $process = Get-Process px_console -ErrorAction Stop |
            Where-Object { $_.Path -eq $targetExe } |
            Select-Object -First 1
        if (-not $process) {
            throw 'Deployed Console did not start.'
        }
        [pscustomobject]@{
            ProcessId = $process.Id
            ExeHash = (Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash
            ConfigHash = (Get-FileHash -LiteralPath $targetConfig -Algorithm SHA256).Hash
            RdpKeyLength = (Get-Item -LiteralPath $rdpMasterKey).Length
            TaskState = (Get-ScheduledTask -TaskName 'Pixels-Console-Debug').State
        }
    }
} finally {
    if ($session) {
        Remove-PSSession $session
    }
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $previousTrustedHosts -Force
}
