#requires -Version 7.0

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({
        $candidate = $null
        [Uri]::TryCreate($_, [UriKind]::Absolute, [ref]$candidate) -and
            $candidate.Scheme -eq 'https' -and
            -not $candidate.Query -and
            -not $candidate.Fragment
    })]
    [string]$ConsoleBase,
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$CertificateAuthority,
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string]$RecordingFileName,
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$RecordingSha256,
    [Parameter(Mandatory)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$RecordingSizeBytes
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$consoleDirectory = Join-Path $repository 'web/px_console'
$credentialsPath = Join-Path $repository '.env/public_test_user.json'
if (-not (Test-Path -LiteralPath $credentialsPath -PathType Leaf)) {
    throw "Public test credentials are missing: $credentialsPath"
}

$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::CreateFromPem(
    [IO.File]::ReadAllText((Resolve-Path -LiteralPath $CertificateAuthority).Path))
$certificateThumbprint = $certificate.Thumbprint
$rootStore = [Security.Cryptography.X509Certificates.X509Store]::new(
    [Security.Cryptography.X509Certificates.StoreName]::Root,
    [Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
$certificateAdded = $false
$previousEnvironment = @{}
$environmentNames = @(
    'PIXELS_PUBLIC_CONSOLE_URL',
    'PIXELS_PUBLIC_CONSOLE_USERNAME',
    'PIXELS_PUBLIC_CONSOLE_PASSWORD',
    'PIXELS_PUBLIC_RECORDING_FILE',
    'PIXELS_PUBLIC_RECORDING_SHA256',
    'PIXELS_PUBLIC_RECORDING_SIZE'
)

try {
    $rootStore.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $existingCertificate = $rootStore.Certificates |
        Where-Object { $_.Thumbprint -eq $certificateThumbprint } |
        Select-Object -First 1
    if (-not $existingCertificate) {
        $rootStore.Add($certificate)
        $certificateAdded = $true
    }

    $credentials = Get-Content -LiteralPath $credentialsPath -Raw | ConvertFrom-Json
    foreach ($environmentName in $environmentNames) {
        $previousEnvironment[$environmentName] = [Environment]::GetEnvironmentVariable($environmentName, 'Process')
    }
    $env:PIXELS_PUBLIC_CONSOLE_URL = $ConsoleBase.TrimEnd('/')
    $env:PIXELS_PUBLIC_CONSOLE_USERNAME = [string]$credentials.username
    $env:PIXELS_PUBLIC_CONSOLE_PASSWORD = [string]$credentials.password
    $env:PIXELS_PUBLIC_RECORDING_FILE = $RecordingFileName
    $env:PIXELS_PUBLIC_RECORDING_SHA256 = $RecordingSha256.ToUpperInvariant()
    $env:PIXELS_PUBLIC_RECORDING_SIZE = [string]$RecordingSizeBytes

    Push-Location $consoleDirectory
    try {
        & npx playwright test --config=playwright.public.config.ts
        if ($LASTEXITCODE -ne 0) {
            throw "Public recording browser acceptance failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }

    [pscustomobject]@{
        Result = 'PASS'
        ConsoleBase = $ConsoleBase.TrimEnd('/')
        RecordingFileName = $RecordingFileName
        RecordingSha256 = $RecordingSha256.ToUpperInvariant()
        RecordingSizeBytes = $RecordingSizeBytes
        Browser = 'Chrome'
        CertificateValidation = 'CurrentUser custom root'
    }
}
finally {
    foreach ($environmentName in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $environmentName,
            $previousEnvironment[$environmentName],
            'Process')
    }
    if ($certificateAdded) {
        $installedCertificate = $rootStore.Certificates |
            Where-Object { $_.Thumbprint -eq $certificateThumbprint } |
            Select-Object -First 1
        if ($installedCertificate) {
            $rootStore.Remove($installedCertificate)
        }
    }
    $rootStore.Dispose()
    $certificate.Dispose()
}
