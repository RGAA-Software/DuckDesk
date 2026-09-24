#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$HealthUrl = 'https://39.71.45.66:4600/health/ready',
    [string]$CertificatePath = 'D:\PixelsServer\app\tls\console.crt',
    [string]$StatePath = 'D:\PixelsServer\data\console-health\state.json',
    [ValidateRange(1, 10)]
    [int]$FailureThreshold = 2
)

$ErrorActionPreference = 'Stop'
$eventSource = 'PixelsConsoleHealth'
if (-not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) {
    throw 'Console health probe certificate is missing.'
}

$previousState = [pscustomobject]@{ consecutive_failures = 0; alerting = $false }
if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
    $previousState = Get-Content -LiteralPath $StatePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($null -eq $previousState.consecutive_failures -or $null -eq $previousState.alerting -or
        [int]$previousState.consecutive_failures -lt 0) {
        throw 'Console health probe state is invalid.'
    }
}

Add-Type -AssemblyName System.Net.Http
Add-Type -TypeDefinition @'
using System;
using System.Net.Http;
using System.Net.Security;
using System.Security.Cryptography.X509Certificates;

public static class PixelsConsoleHealthCertificatePin {
    public static string ExpectedThumbprint;

    public static bool Validate(HttpRequestMessage request, X509Certificate2 certificate,
                                X509Chain chain, SslPolicyErrors errors) {
        return certificate != null && DateTime.UtcNow >= certificate.NotBefore.ToUniversalTime()
            && DateTime.UtcNow <= certificate.NotAfter.ToUniversalTime()
            && String.Equals(certificate.Thumbprint, ExpectedThumbprint,
                StringComparison.OrdinalIgnoreCase);
    }
}
'@ -ReferencedAssemblies @('System.Net.Http.dll', 'System.dll')
$expectedCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($CertificatePath)
try {
    [PixelsConsoleHealthCertificatePin]::ExpectedThumbprint = $expectedCertificate.Thumbprint
} finally {
    $expectedCertificate.Dispose()
}
$handler = [Net.Http.HttpClientHandler]::new()
$handler.UseProxy = $false
$validationDelegateType = [System.Func[
    Net.Http.HttpRequestMessage,
    Security.Cryptography.X509Certificates.X509Certificate2,
    Security.Cryptography.X509Certificates.X509Chain,
    Net.Security.SslPolicyErrors,
    bool
]]
$handler.ServerCertificateCustomValidationCallback = [Delegate]::CreateDelegate(
    $validationDelegateType,
    [PixelsConsoleHealthCertificatePin].GetMethod('Validate')
)
$httpClient = [Net.Http.HttpClient]::new($handler)
$httpClient.Timeout = [TimeSpan]::FromSeconds(4)
$httpStatus = 'transport_error'
try {
    $response = $httpClient.GetAsync($HealthUrl).GetAwaiter().GetResult()
    try {
        $httpStatus = [string][int]$response.StatusCode
    } finally {
        $response.Dispose()
    }
} catch {
    $httpStatus = 'transport_error'
} finally {
    $httpClient.Dispose()
}
$healthy = $httpStatus -eq '204'
$failureCount = if ($healthy) { 0 } else { [int]$previousState.consecutive_failures + 1 }
$alerting = [bool]$previousState.alerting
$transition = 'none'
if ($healthy -and $alerting) {
    Write-EventLog -LogName Application -Source $eventSource -EntryType Information -EventId 4102 `
        -Message "Pixels Console readiness recovered: $HealthUrl"
    $alerting = $false
    $transition = 'recovered'
} elseif (-not $healthy -and -not $alerting -and $failureCount -ge $FailureThreshold) {
    Write-EventLog -LogName Application -Source $eventSource -EntryType Error -EventId 4101 `
        -Message "Pixels Console readiness failed $failureCount consecutive checks: $HealthUrl"
    $alerting = $true
    $transition = 'failed'
}

$stateDirectory = Split-Path -Path $StatePath -Parent
[void](New-Item -ItemType Directory -Path $stateDirectory -Force)
$nextState = @{
    consecutive_failures = $failureCount
    alerting = $alerting
    checked_at_utc = [DateTime]::UtcNow.ToString('o')
    http_status = $httpStatus
}
$temporaryPath = "$StatePath.$PID.tmp"
[IO.File]::WriteAllText($temporaryPath, ($nextState | ConvertTo-Json -Compress), [Text.UTF8Encoding]::new($false))
if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
    [IO.File]::Replace($temporaryPath, $StatePath, "$StatePath.previous")
} else {
    [IO.File]::Move($temporaryPath, $StatePath)
}

[pscustomobject]@{
    Healthy = $healthy
    ConsecutiveFailures = $failureCount
    Alerting = $alerting
    Transition = $transition
}
