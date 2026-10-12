#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$ProbeBinary = '',
    [Parameter(Mandatory)]
    [string]$ServicePolicyBinary,
    [string]$EvidenceDirectory = '',
    [switch]$Distribution,
    [string]$WindowsRelayBinary = '',
    [string]$LinuxRelayImage = 'pixels-relay:3.2.1-delivery-20261011'
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
if ($Distribution) {
    if (-not $WindowsRelayBinary) { $WindowsRelayBinary = Join-Path $repository 'build_official/private_server/candidates/relay-retention-20261012/package/bin/px_relay.exe' }
    $binaryPaths['PIXELS_TEST_WINDOWS_RELAY_BINARY'] = [IO.Path]::GetFullPath($WindowsRelayBinary)
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
if ($Distribution) {
    $dockerBinary = Join-Path $env:ProgramFiles 'Docker/Docker/resources/bin/docker.exe'
    if (-not (Test-Path -LiteralPath $dockerBinary -PathType Leaf)) { $dockerBinary = (Get-Command docker -ErrorAction Stop).Source }
    $imageInventory = & $dockerBinary image inspect $LinuxRelayImage
    if ($LASTEXITCODE -ne 0) { throw 'The verified Linux Relay image must already be loaded' }
    $image = ($imageInventory | ConvertFrom-Json)[0]
    if ($image.Config.Labels.'io.pixels.product' -ne 'relay' -or -not $image.Config.Labels.'io.pixels.runtime.sha256') { throw 'Linux Relay identity missing' }
    $testEnvironment['PIXELS_TEST_DOCKER_BINARY'] = $dockerBinary
    $testEnvironment['PIXELS_TEST_LINUX_RELAY_IMAGE'] = $image.Id
    $testEnvironment['PIXELS_TEST_LINUX_RELAY_SHA256'] = $image.Config.Labels.'io.pixels.runtime.sha256'
    $identities['linux_relay'] = @{image_id=$image.Id;runtime_sha256=$image.Config.Labels.'io.pixels.runtime.sha256'}
}
try {
    foreach ($environmentName in $testEnvironment.Keys) {
        $savedEnvironment[$environmentName] = [Environment]::GetEnvironmentVariable($environmentName)
        [Environment]::SetEnvironmentVariable($environmentName, $testEnvironment[$environmentName])
    }
    $suite = if ($Distribution) { 'relay-distribution' } else { 'relay-retention' }
    & (Join-Path $PSScriptRoot 'postgres.ps1') TestSuite -Suite $suite
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
