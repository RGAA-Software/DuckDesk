#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$ProbeBinary = '',
    [Parameter(Mandatory)]
    [string]$ServicePolicyBinary,
    [string]$EvidenceDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $ProbeBinary) { $ProbeBinary = Join-Path $repository 'rust_transport/target/release/px_transport_probe.exe' }
if (-not $EvidenceDirectory) { $EvidenceDirectory = Join-Path $repository '.cache/relay-retention-integration' }
$EvidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
[IO.Directory]::CreateDirectory($EvidenceDirectory) | Out-Null
$binaryPaths = @{
    PIXELS_TEST_TRANSPORT_PROBE_BINARY = [IO.Path]::GetFullPath($ProbeBinary)
    PIXELS_TEST_SERVICE_POLICY_BINARY = [IO.Path]::GetFullPath($ServicePolicyBinary)
}
$identities = @{}
foreach ($binaryName in $binaryPaths.Keys) {
    $binaryPath = $binaryPaths[$binaryName]
    if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) { throw "Build the Release test helper first: $binaryPath" }
    $identities[$binaryName] = @{path=$binaryPath;sha256=(Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash}
}
$savedEnvironment = @{}
$testEnvironment = $binaryPaths.Clone()
$testEnvironment['PIXELS_TEST_RELAY_RETENTION_RESULT'] = Join-Path $EvidenceDirectory 'result.json'
$testEnvironment['CARGO_BUILD_JOBS'] = '18'
try {
    foreach ($environmentName in $testEnvironment.Keys) {
        $savedEnvironment[$environmentName] = [Environment]::GetEnvironmentVariable($environmentName)
        [Environment]::SetEnvironmentVariable($environmentName, $testEnvironment[$environmentName])
    }
    & (Join-Path $PSScriptRoot 'postgres.ps1') TestSuite -Suite relay-retention
    if ($LASTEXITCODE -ne 0) { throw "Relay retention integration failed: $LASTEXITCODE" }
    foreach ($binaryName in $binaryPaths.Keys) {
        if ((Get-FileHash -LiteralPath $binaryPaths[$binaryName] -Algorithm SHA256).Hash -ne $identities[$binaryName].sha256) {
            throw "Test helper changed during validation: $binaryName"
        }
    }
    if (-not (Test-Path -LiteralPath $testEnvironment.PIXELS_TEST_RELAY_RETENTION_RESULT)) { throw 'Integration result missing' }
    $identities | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'identities.json') -Encoding utf8
    Write-Host "PASS RELAY RETENTION: Console policy, actual Service heartbeat and held QUIC streams/datagrams; evidence $EvidenceDirectory"
} finally {
    foreach ($environmentName in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($environmentName, $savedEnvironment[$environmentName])
    }
}
