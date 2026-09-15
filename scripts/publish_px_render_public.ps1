#requires -Version 7.0

param(
    [string]$ComputerName = '39.71.45.66'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repository 'build_official/dist/px_render.exe'
if (-not (Test-Path -LiteralPath $source)) {
    throw "Required deployment input is missing: $source"
}

$expectedHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$oldTrustedHosts = (Get-Item WSMan:\localhost\Client\TrustedHosts).Value
$session = $null
try {
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $ComputerName -Force
    $machineText = Get-Content -LiteralPath (Join-Path $repository '.env/test_machine.md') -Raw
    $password = [regex]::Match($machineText, '(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
    if (-not $password) {
        throw 'Node deployment credential is missing'
    }
    $machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
    $userName = if ($machineName) { "$machineName\Administrator" } else { 'Administrator' }
    $credential = [pscredential]::new($userName, (ConvertTo-SecureString $password -AsPlainText -Force))
    $session = New-PSSession -ComputerName $ComputerName -Credential $credential
    $staging = 'D:/software/esprit_169811/render/px_render.staged.exe'
    Copy-Item -LiteralPath $source -Destination $staging -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList $expectedHash -ScriptBlock {
        param($expected)

        $target = 'D:/software/esprit_169811/render/px_render.exe'
        $staging = 'D:/software/esprit_169811/render/px_render.staged.exe'
        if ((Get-FileHash -LiteralPath $staging -Algorithm SHA256).Hash -ne $expected) {
            throw 'Render staging hash mismatch'
        }

        $services = @(Get-CimInstance Win32_Service | Where-Object { $_.PathName -match 'esprit_169811.*px_service\.exe' })
        if ($services.Count -ne 1) {
            throw 'Service identity is ambiguous'
        }
        $serviceName = $services[0].Name
        Stop-Service -Name $serviceName -Force
        $deadline = (Get-Date).AddSeconds(20)
        do {
            Start-Sleep -Milliseconds 250
            $state = (Get-Service -Name $serviceName).Status
        } until ($state -eq 'Stopped' -or (Get-Date) -ge $deadline)
        if ($state -ne 'Stopped') {
            throw 'Service did not stop before Render deployment'
        }

        Get-Process px_render, px_panel -ErrorAction SilentlyContinue | Stop-Process -Force
        Copy-Item -LiteralPath $staging -Destination $target -Force
        Remove-Item -LiteralPath $staging -Force
        $actualHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
        if ($actualHash -ne $expected) {
            throw 'Render deployment hash mismatch'
        }

        Start-Service -Name $serviceName
        $deadline = (Get-Date).AddSeconds(20)
        do {
            Start-Sleep -Milliseconds 250
            $state = (Get-Service -Name $serviceName).Status
        } until ($state -eq 'Running' -or (Get-Date) -ge $deadline)
        if ($state -ne 'Running') {
            throw 'Service did not restart after Render deployment'
        }
        [pscustomobject]@{
            Service = $serviceName
            State = $state
            RenderHash = $actualHash
        }
    }
}
finally {
    if ($session) {
        Remove-PSSession $session
    }
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $oldTrustedHosts -Force
}
