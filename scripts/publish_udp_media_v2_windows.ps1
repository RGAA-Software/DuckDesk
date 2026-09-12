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
        $relativeSource=if($name -eq 'px_client'){'build_official/src/px_deps/px_client.exe'}else{"build_official/src/$name/$name.exe"}
        $source=Join-Path $repo $relativeSource
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
        $renderRoot='D:/software/esprit_169811/render'
        $rdpWorkspaceRoot='D:/software/esprit_169811/render/rdp/workspaces'
        if(-not (Test-Path -LiteralPath $rollback)){throw 'Original runtime backup missing'}
        if((Get-FileHash $staged).Hash -ne $expected){throw 'Staging hash mismatch'}
        $owned=Get-CimInstance Win32_Service | Where-Object {$_.PathName -match 'esprit_169811.*px_service.exe'}
        if(@($owned).Count -ne 1){throw 'Service identity ambiguous'}
        $wasRunning=$owned.State -eq 'Running'
        try {
            if($wasRunning){
                Stop-Service $owned.Name -Force
                $deadline=(Get-Date).AddSeconds(15)
                do {
                    Start-Sleep -Milliseconds 300
                    $state=(Get-Service $owned.Name).Status
                } until($state -eq 'Stopped' -or (Get-Date)-ge$deadline)
                if($state -ne 'Stopped'){throw 'Service did not stop before Render publication'}
            }
            $renderProcesses=Get-Process px_render -ErrorAction SilentlyContinue |
                Where-Object {($_.Path -replace '\\','/') -eq $target}
            foreach($process in $renderProcesses){
                try {
                    $process.Kill()
                    if(-not $process.WaitForExit(5000)){throw 'Render did not stop before publishing'}
                } catch [System.InvalidOperationException] {
                    # The service and child render shutdown race is expected.
                }
            }
            if(Test-Path -LiteralPath $rdpWorkspaceRoot){
                $resolvedRenderRoot=[IO.Path]::GetFullPath($renderRoot)
                $resolvedWorkspaceRoot=[IO.Path]::GetFullPath($rdpWorkspaceRoot)
                if(-not $resolvedWorkspaceRoot.StartsWith($resolvedRenderRoot+'\',[StringComparison]::OrdinalIgnoreCase)){
                    throw 'Refusing to update an RDP workspace ACL outside the Render installation directory'
                }
                $directories=@(Get-Item -LiteralPath $resolvedWorkspaceRoot)+@(Get-ChildItem -LiteralPath $resolvedWorkspaceRoot -Directory -Recurse)
                foreach($directory in $directories){
                    & icacls.exe $directory.FullName /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' /C /Q | Out-Null
                    if($LASTEXITCODE -ne 0){throw "RDP workspace directory ACL deployment failed: $($directory.FullName)"}
                }
                foreach($file in Get-ChildItem -LiteralPath $resolvedWorkspaceRoot -File -Recurse){
                    & icacls.exe $file.FullName /inheritance:r /grant:r '*S-1-5-18:F' '*S-1-5-32-544:F' /C /Q | Out-Null
                    if($LASTEXITCODE -ne 0){throw "RDP workspace file ACL deployment failed: $($file.FullName)"}
                }
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
