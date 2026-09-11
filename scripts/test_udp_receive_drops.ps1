#requires -Version 7.0
param([ValidateRange(576,1400)][int]$DatagramSize=1400)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$output=Join-Path $repo ('test-results/udp-media-v2/drops-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output | Out-Null
if((& pktmon status | Out-String) -notmatch 'not running'){throw 'Packet monitor is already in use'}
if((& pktmon filter list | Out-String) -notmatch '\bNone\b'){throw 'Existing filters belong to another diagnostic'}
$filter=$false
$started=$false
try {
    & pktmon filter add PixelsReceiveDrops -t UDP -p 4613
    if($LASTEXITCODE){throw 'Filter creation failed'}
    $filter=$true
    & pktmon start --capture --comp all --type drop --pkt-size 96 --file-size 2048 --file-name "$output/drops.etl"
    if($LASTEXITCODE){throw 'Drop capture failed to start'}
    $started=$true
    Write-Output "DROP_CAPTURE output=$output"
    # A nonce-bound diagnostic sender, no game/client/FEC and no network setting changes.
    & (Join-Path $PSScriptRoot 'test_node90_udp_link.ps1') -StageSeconds 5 -DatagramSize $DatagramSize
} finally {
    try {
        if($started){
            try {& pktmon counters | Set-Content "$output/counters.txt" -Encoding utf8}
            finally {& pktmon stop}
        }
    } finally {
        if($filter){
            $filters=& pktmon filter list | Out-String
            if(($filters -split "`n" | Where-Object {$_ -match '^\s*\d+\s+'}).Count -eq 1 -and $filters -match 'PixelsReceiveDrops'){
                & pktmon filter remove
            }
        }
    }
}
& pktmon etl2txt "$output/drops.etl" --out "$output/drops.txt"
if($LASTEXITCODE){throw 'Drop trace conversion failed'}
Write-Output "DROP_TRACE=$output/drops.txt"
