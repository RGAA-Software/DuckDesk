#requires -Version 7.0
param([switch]$Render)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$dist=Join-Path $repo 'build_official/dist'
$names=if($Render){@('px_client','px_render')}else{@('px_client')}
$service=Get-Service px_service
$restart=$Render -and $service.Status -eq 'Running'
try {
    if($restart){Stop-Service px_service -Force}
    foreach($name in $names){
        $source=Join-Path $repo "build_official/src/$name/$name.exe"
        $target=Join-Path $dist "$name.exe"
        Get-Process $name -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $target} | Stop-Process -Force
        Copy-Item -LiteralPath $source -Destination $target -Force
        $hash=(Get-FileHash $source).Hash
        if($hash -ne (Get-FileHash $target).Hash){throw "Publish hash mismatch: $name"}
        Write-Output "$name SHA256=$hash"
    }
} finally {if($restart){Start-Service px_service}}
if(-not $Render){return}
$oldTrusted=(Get-Item WSMan:\localhost\Client\TrustedHosts).Value
$session=$null
try {
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value '39.71.45.66' -Force
    $machineText=Get-Content (Join-Path $repo '.env/test_machine.md') -Raw
    $secret=[regex]::Match($machineText,'(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
    $credential=[pscredential]::new('administrator',(ConvertTo-SecureString $secret -AsPlainText -Force))
    $session=New-PSSession -ComputerName 39.71.45.66 -Credential $credential
    $source=Join-Path $dist 'px_render.exe'
    Copy-Item -LiteralPath $source -Destination 'D:/software/esprit_169811/udp-fec-validation/px_render-staged.exe' -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList (Get-FileHash $source).Hash -ScriptBlock {
        param($expected)
        $target='D:/software/esprit_169811/render/px_render.exe'
        $staged='D:/software/esprit_169811/udp-fec-validation/px_render-staged.exe'
        $rollback='D:/software/esprit_169811/before-udp-media-v2-20260910-230800/px_render.exe'
        if(-not (Test-Path -LiteralPath $rollback)){throw 'Original runtime backup missing'}
        if((Get-FileHash $staged).Hash -ne $expected){throw 'Staging hash mismatch'}
        $owned=Get-CimInstance Win32_Service | Where-Object {$_.PathName -match 'esprit_169811.*px_service.exe'}
        if(@($owned).Count -ne 1){throw 'Service identity ambiguous'}
        $wasRunning=$owned.State -eq 'Running'
        try {
            if($wasRunning){Stop-Service $owned.Name -Force}
            $renderProcesses=Get-Process px_render -ErrorAction SilentlyContinue |
                Where-Object {($_.Path -replace '\\','/') -eq $target}
            foreach($process in $renderProcesses){
                $process.Kill()
                if(-not $process.WaitForExit(5000)){throw 'Render did not stop before publishing'}
            }
            Copy-Item -LiteralPath $staged -Destination $target -Force
            $actual=(Get-FileHash $target).Hash
            if($actual -ne $expected){throw 'Deployment hash mismatch'}
            Write-Output "NODE90_RENDER SHA256=$actual"
        } finally {if($wasRunning){Start-Service $owned.Name}}
    }
} finally {
    if($session){Remove-PSSession $session}
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value $oldTrusted -Force
}
