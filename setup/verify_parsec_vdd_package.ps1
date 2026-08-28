param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot
)

$ErrorActionPreference = "Stop"

$expected = @{
    "nefconw.exe" = "CF746D1B0BBB713993D4A90DCCD774C78D9FFF8C2BA5A054B6C8F56C77E1EEE1"
    "driver\mm.cat" = "136E64AC07DCE5A3B4935D5A9C5CFE03983C0B3065F46A30A45536D5B1681D5C"
    "driver\mm.dll" = "96DB6AE2F950B56E52BE3E68F92893AFA94645EAE09FEA2ABD5DD1985758150A"
    "driver\mm.inf" = "34DA9FF45C13577631F67E33D11B8A26E3D22CA685D00C388B6122A795800588"
}

foreach ($entry in $expected.GetEnumerator()) {
    $path = Join-Path $PackageRoot $entry.Key
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Parsec VDD package file is missing: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $entry.Value) {
        throw "Parsec VDD SHA-256 mismatch for $path; expected $($entry.Value), got $actual"
    }
}

foreach ($relative in @("driver\mm.cat", "driver\mm.dll")) {
    $path = Join-Path $PackageRoot $relative
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    if ($signature.Status -ne "Valid") {
        throw "Parsec VDD signature is not valid for ${path}: $($signature.StatusMessage)"
    }
    if ($signature.SignerCertificate.Subject -notlike "*Microsoft Windows Hardware Compatibility Publisher*") {
        throw "Unexpected Parsec VDD signer for ${path}: $($signature.SignerCertificate.Subject)"
    }
}

exit 0
