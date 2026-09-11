#requires -Version 7.0
param([ValidateRange(30,180)][int]$Seconds=180)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$output=Join-Path $repo ('test-results/udp-media-v2/wire-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$oldTrusted=(Get-Item WSMan:\localhost\Client\TrustedHosts).Value
$session=$null
$localStarted=$false
$remoteStarted=$false
$localFilter=$false
$remoteFilter=$false
$clientLog='C:/Users/Public/Pixels/px_logs/app.39.71.45.66.log'
$clientLines=if(Test-Path -LiteralPath $clientLog){@(Get-Content -LiteralPath $clientLog).Count}else{0}
$renderLines=$null
try {
    if((& pktmon status | Out-String) -notmatch 'not running'){throw 'Local packet monitor is already in use'}
    if((& pktmon filter list | Out-String) -notmatch '\bNone\b'){throw 'Local packet filters belong to another diagnostic session'}
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value '39.71.45.66' -Force
    $machineText=Get-Content (Join-Path $repo '.env/test_machine.md') -Raw
    $secret=[regex]::Match($machineText,'(?m)^\s*-\s*密码\s*[:：]\s*(.+?)\s*$').Groups[1].Value
    $session=New-PSSession -ComputerName 39.71.45.66 -Credential ([pscredential]::new('administrator',(ConvertTo-SecureString $secret -AsPlainText -Force)))
    $renderLines=Invoke-Command -Session $session -ScriptBlock {
        $path='C:/Users/Public/Pixels/px_logs/pixels_render_4613.log'
        if(Test-Path -LiteralPath $path){@(Get-Content -LiteralPath $path).Count}else{0}
    }
    Invoke-Command -Session $session -ScriptBlock {
        function global:Invoke-PixelsPktmon([string]$Arguments) {
            $info=[Diagnostics.ProcessStartInfo]::new('pktmon.exe',$Arguments)
            $info.UseShellExecute=$false
            $info.CreateNoWindow=$true
            $info.RedirectStandardOutput=$true
            $info.StandardOutputEncoding=[Text.Encoding]::UTF8
            $process=[Diagnostics.Process]::Start($info)
            try {
                $result=$process.StandardOutput.ReadToEnd()
                $process.WaitForExit()
                if($process.ExitCode){throw "Remote packet monitor failed: $Arguments ($($process.ExitCode))"}
                return $result
            } finally {$process.Dispose()}
        }
        $status=Invoke-PixelsPktmon 'status'
        $filters=Invoke-PixelsPktmon 'filter list'
        if($status -notmatch 'not running|没有运行'){throw "Remote packet monitor is already in use: $status"}
        if($filters -notmatch '\bNone\b|无'){throw "Remote packet filters belong to another diagnostic session: $filters"}
    }
    $remoteDirectory='D:/software/esprit_169811/udp-fec-validation/'+(Split-Path $output -Leaf)
    Invoke-Command -Session $session -ArgumentList $remoteDirectory -ScriptBlock {
        param($directory)
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        Invoke-PixelsPktmon 'filter add PixelsMediaAcceptance -t UDP -p 4613'
    }
    $remoteFilter=$true
    Invoke-Command -Session $session -ArgumentList $remoteDirectory -ScriptBlock {
        param($directory)
        # Pktmon uses 16 MiB per-CPU buffers here. A small circular file silently overwrites other CPU buffers on stop.
        # The limit must exceed the reported 768 MiB buffers; the bounded header-only trace compresses to much less on disk.
        Invoke-PixelsPktmon "start --capture --comp nics --pkt-size 96 --file-size 2048 --file-name $directory/send.etl"
    }
    $remoteStarted=$true
    & pktmon filter add PixelsMediaAcceptance -t UDP -p 4613
    if($LASTEXITCODE){throw 'Local filter creation failed'}
    $localFilter=$true
    & pktmon start --capture --comp nics --pkt-size 96 --file-size 2048 --file-name "$output/receive.etl"
    if($LASTEXITCODE){throw 'Local capture start failed'}
    $localStarted=$true
    Get-NetAdapterStatistics | Select-Object Name,ReceivedPacketErrors,ReceivedDiscardedPackets |
        ConvertTo-Json | Set-Content -LiteralPath "$output/nic-before.json"
    Write-Output "WIRE_CAPTURE output=$output"
    & (Join-Path $PSScriptRoot 'test_udp_media_v2_windows.ps1') -ObserveSeconds $Seconds -MouseSweep -MouseSweepSeconds ([Math]::Min(120,$Seconds-20))
} finally {
  try {
    if($localStarted){
        Get-NetAdapterStatistics | Select-Object Name,ReceivedPacketErrors,ReceivedDiscardedPackets |
            ConvertTo-Json | Set-Content -LiteralPath "$output/nic-after.json"
        & pktmon counters | Set-Content -LiteralPath "$output/receive-counters.txt"
        & pktmon stop
    }
    if($remoteStarted){
        Invoke-Command -Session $session -ScriptBlock {Invoke-PixelsPktmon 'counters'} |
            Set-Content -LiteralPath "$output/send-counters.txt"
        Invoke-Command -Session $session -ScriptBlock {Invoke-PixelsPktmon 'stop'}
    }
    # Only the media timing/window records are exported, never tickets, credentials or media payloads.
    if(Test-Path -LiteralPath $clientLog){
        Get-Content -LiteralPath $clientLog | Select-Object -Skip $clientLines |
            Where-Object {$_ -match 'UDP timing |UDP media v2 window:'} | Set-Content -LiteralPath "$output/client.log" -Encoding UTF8
    }
    if($session -and $null -ne $renderLines){
        Invoke-Command -Session $session -ArgumentList $renderLines -ScriptBlock {
            param($skip)
            $path='C:/Users/Public/Pixels/px_logs/pixels_render_4613.log'
            if(Test-Path -LiteralPath $path){
                Get-Content -LiteralPath $path | Select-Object -Skip $skip |
                    Where-Object {$_ -match 'UDP timing |UDP media v2:|UDP incomplete send:|Video backlog rejected'}
            }
        } | Set-Content -LiteralPath "$output/render.log" -Encoding UTF8
    }
    # Both monitors had no filters before this diagnostic. Do not clear filters if another owner added one meanwhile.
    if($localFilter){
        $filters=& pktmon filter list | Out-String
        if(($filters -split "`n" | Where-Object {$_ -match '^\s*\d+\s+'}).Count -eq 1 -and $filters -match 'PixelsMediaAcceptance'){
            & pktmon filter remove
        }
    }
    if($remoteFilter){Invoke-Command -Session $session -ScriptBlock {
        $filters=Invoke-PixelsPktmon 'filter list'
        if(($filters -split "`n" | Where-Object {$_ -match '^\s*\d+\s+'}).Count -eq 1 -and $filters -match 'PixelsMediaAcceptance'){
            Invoke-PixelsPktmon 'filter remove'
        }
    }}
    if($remoteStarted){
        Invoke-Command -Session $session -ArgumentList $remoteDirectory -ScriptBlock {
            param($directory)
            Invoke-PixelsPktmon "etl2pcap $directory/send.etl --out $directory/send.pcapng"
        }
        Copy-Item -FromSession $session -LiteralPath "$remoteDirectory/send.pcapng" -Destination "$output/send.pcapng"
    }
    if($localStarted){& pktmon etl2pcap "$output/receive.etl" --out "$output/receive.pcapng"}
  } finally {
    try {if($session){Remove-PSSession $session}} finally {
        Set-Item WSMan:\localhost\Client\TrustedHosts -Value $oldTrusted -Force
    }
  }
}
