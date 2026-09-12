#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repository 'rust_client/target/release/px_service.exe'
$configSource = Join-Path $repository 'rust_client/px_service/px_service.toml'
$expected = (Get-FileHash $source).Hash
$expectedConfig = (Get-FileHash $configSource).Hash
$oldTrustedHosts = (Get-Item WSMan:\localhost\Client\TrustedHosts).Value
$session = $null
try {
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value '39.71.45.66' -Force
    $machineText = Get-Content (Join-Path $repository '.env/test_machine.md') -Raw
    $password = [regex]::Match($machineText, '(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
    $credential = [pscredential]::new('administrator', (ConvertTo-SecureString $password -AsPlainText -Force))
    $session = New-PSSession -ComputerName 39.71.45.66 -Credential $credential
    $staging = 'D:/software/esprit_169811/render/px_service.staged.exe'
    $configStaging = 'D:/software/esprit_169811/render/px_service.staged.toml'
    Copy-Item -LiteralPath $source -Destination $staging -ToSession $session -Force
    Copy-Item -LiteralPath $configSource -Destination $configStaging -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList $expected,$expectedConfig -ScriptBlock {
        param($expectedHash, $expectedConfigHash)
        $target = 'D:/software/esprit_169811/render/px_service.exe'
        $staging = 'D:/software/esprit_169811/render/px_service.staged.exe'
        $configTarget = 'D:/software/esprit_169811/render/px_service.toml'
        $configStaging = 'D:/software/esprit_169811/render/px_service.staged.toml'
        if ((Get-FileHash $staging).Hash -ne $expectedHash) { throw 'staging hash mismatch' }
        if ((Get-FileHash $configStaging).Hash -ne $expectedConfigHash) { throw 'configuration staging hash mismatch' }
        $service = @(Get-CimInstance Win32_Service | Where-Object { $_.PathName -match 'esprit_169811.*px_service.exe' })
        if ($service.Count -ne 1) { throw 'service identity ambiguous' }
        $wasRunning = $service[0].State -eq 'Running'
        if ($wasRunning) {
            Stop-Service $service[0].Name -Force
            $deadline = (Get-Date).AddSeconds(15)
            do {
                Start-Sleep -Milliseconds 300
                $state = (Get-Service $service[0].Name).Status
            } until ($state -eq 'Stopped' -or (Get-Date) -ge $deadline)
            if ($state -ne 'Stopped') { throw 'service did not stop before deployment' }
        }
        Copy-Item -LiteralPath $staging -Destination $target -Force
        Copy-Item -LiteralPath $configStaging -Destination $configTarget -Force
        Remove-Item -LiteralPath $staging -Force
        Remove-Item -LiteralPath $configStaging -Force
        if ((Get-FileHash $target).Hash -ne $expectedHash) { throw 'deployment hash mismatch' }
        if ((Get-FileHash $configTarget).Hash -ne $expectedConfigHash) { throw 'configuration deployment hash mismatch' }
        Start-Service $service[0].Name
        $deadline = (Get-Date).AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 300
            $state = (Get-Service $service[0].Name).Status
        } until ($state -eq 'Running' -or (Get-Date) -ge $deadline)
        [pscustomobject]@{
            Service = $service[0].Name
            State = $state
            Hash = (Get-FileHash $target).Hash
            ConfigHash = (Get-FileHash $configTarget).Hash
        }
    }
}
finally {
    if ($session) { Remove-PSSession $session }
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $oldTrustedHosts -Force
}
