param(
    [string]$TargetHost = '10.0.0.90',
    [ValidateRange(1, 65535)]
    [int]$TargetPort = 20371,
    [string]$DeviceId = '001190520'
)

$ErrorActionPreference = 'Stop'

function Invoke-RejectedDirectAllocation(
    [string]$Name,
    [hashtable]$Payload,
    [int]$ExpectedBusinessCode,
    [switch]$OmitDeviceId,
    [string]$StreamId = ''
) {
    $deviceQuery = if ($OmitDeviceId) { '' } else { "device_id=$DeviceId&" }
    $streamQuery = if ($StreamId) { "stream_id=$StreamId&" } else { '' }
    $uri = "http://${TargetHost}:$TargetPort/alloc/local/rtc?${deviceQuery}${streamQuery}safety_pwd_md5=invalid"
    $bodyPath = Join-Path $env:TEMP "px_direct_negative_$([guid]::NewGuid().ToString('N')).json"
    try {
        $Payload | ConvertTo-Json -Compress | Set-Content -LiteralPath $bodyPath -NoNewline -Encoding utf8
        $response = & curl.exe --silent --show-error --output - --write-out "`n%{http_code}" `
            --request POST --header 'Content-Type: application/json' --data-binary "@$bodyPath" $uri
        if ($LASTEXITCODE -ne 0) {
            throw "curl failed for $Name with exit code $LASTEXITCODE"
        }

        $lines = @($response -split "`r?`n")
        $httpStatus = [int]$lines[-1]
        $body = ($lines[0..($lines.Count - 2)] -join "`n") | ConvertFrom-Json
        if ($httpStatus -ne 403 -or $body.code -ne $ExpectedBusinessCode) {
            throw "$Name returned http=$httpStatus code=$($body.code); expected http=403 code=$ExpectedBusinessCode"
        }
        [pscustomobject]@{
            Check = $Name
            HttpStatus = $httpStatus
            BusinessCode = $body.code
            Result = 'PASS'
        }
    }
    finally {
        Remove-Item -LiteralPath $bodyPath -Force -ErrorAction SilentlyContinue
    }
}

$sdp = "v=0`r`n"
Invoke-RejectedDirectAllocation 'forged_direct_grant_rejected' @{
    sdp = $sdp
    client_nonce = "negative_$([guid]::NewGuid().ToString('N'))"
    direct_session_grant = 'invalid-grant'
} 706

Invoke-RejectedDirectAllocation 'invalid_direct_auth_rejected' @{
    sdp = $sdp
    client_nonce = "negative_$([guid]::NewGuid().ToString('N'))"
} 700

# No device id is the password-only IP direct route. Even a supplied forged
# grant must not switch it into device/grant validation; the bad password is
# the only authentication failure reported.
Invoke-RejectedDirectAllocation 'idless_ip_direct_checks_password_only' @{
    sdp = $sdp
    direct_session_grant = 'invalid-grant-that-must-be-ignored'
} 700 -OmitDeviceId

Invoke-RejectedDirectAllocation 'idless_ip_direct_rejects_unprepared_stream' @{
    sdp = $sdp
    client_nonce = "negative_$([guid]::NewGuid().ToString('N'))"
} 707 -OmitDeviceId -StreamId 'ip-direct:unprepared-stream'

Write-Host 'RESULT: PASS'
