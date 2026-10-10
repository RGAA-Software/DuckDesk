param(
    [ValidateSet('Build', 'Test')]
    [string]$Action = 'Build',
    [int]$Jobs = 8,
    [ValidateSet('Release', 'Publication')]
    [string]$Profile = 'Release'
)

$ErrorActionPreference = 'Stop'
$transportRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'rust_transport'
& python (Join-Path $PSScriptRoot '../scripts/tests/verify_iroh_vendor.py')
if ($LASTEXITCODE -ne 0) { throw 'Isolated iroh source verification failed.' }
Push-Location $transportRoot
try {
    $cargoProfile = $Profile.ToLowerInvariant()
    & cargo build --workspace --profile $cargoProfile --locked --jobs $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Transport build failed.' }
    if ($Action -eq 'Test') {
        & cargo test --workspace --profile $cargoProfile --locked --jobs $Jobs
        if ($LASTEXITCODE -ne 0) { throw 'Transport tests failed.' }
        & (Join-Path $transportRoot "target/$cargoProfile/px_transport_probe.exe") self-test
        if ($LASTEXITCODE -ne 0) { throw 'Direct transport probe failed.' }
    }
} finally {
    Pop-Location
}
