[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Console', 'Proxy')][string]$Role,
    [Parameter(Mandatory)][string]$Directory,
    [string[]]$DnsNames = @('localhost'),
    [string[]]$IpAddresses = @('127.0.0.1')
)
# PowerShell 7 / .NET: creates private-deployment trust material, never installs
# a global root, changes RDS policy, overwrites an existing key or logs a secret.
$ErrorActionPreference = 'Stop'
$rdpTrustRoot = [IO.Path]::GetFullPath($Directory)
if (-not [IO.Path]::IsPathFullyQualified($Directory) -or $rdpTrustRoot -eq [IO.Path]::GetPathRoot($rdpTrustRoot)) {
    throw 'An explicit dedicated absolute directory is required'
}
if (Test-Path -LiteralPath $rdpTrustRoot) {
    if ((Get-Item -LiteralPath $rdpTrustRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse directory rejected' }
    if (@(Get-ChildItem -LiteralPath $rdpTrustRoot -Force).Count -ne 0) { throw 'Existing trust material must be retained; refusing replacement' }
} else { New-Item -ItemType Directory -Path $rdpTrustRoot | Out-Null }
$rdpAcl = [Security.AccessControl.DirectorySecurity]::new()
$rdpAcl.SetAccessRuleProtection($true, $false)
foreach ($sid in @([Security.Principal.WindowsIdentity]::GetCurrent().User.Value, 'S-1-5-18', 'S-1-5-32-544') | Select-Object -Unique) {
    $rdpAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),
        'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
}
Set-Acl -LiteralPath $rdpTrustRoot -AclObject $rdpAcl
$rdpEncoding = [Text.UTF8Encoding]::new($false)
$rdpKey = [Security.Cryptography.RSA]::Create(3072)
$rdpCaKey = [Security.Cryptography.RSA]::Create(3072)
try {
    $now = [DateTimeOffset]::UtcNow
    $caRequest = [Security.Cryptography.X509Certificates.CertificateRequest]::new("CN=GammaRay $Role Private Deployment CA",
        $rdpCaKey, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $caRequest.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($true, $true, 0, $true))
    $caRequest.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
        [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyCertSign -bor
        [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::CrlSign, $true))
    $ca = $caRequest.CreateSelfSigned($now.AddMinutes(-5), $now.AddYears(5))
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new("CN=GammaRay $Role",
        $rdpKey, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false, $false, 0, $true))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
        [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature -bor
        [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment, $true))
    $eku = [Security.Cryptography.OidCollection]::new()
    [void]$eku.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1'))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($eku, $true))
    $san = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    foreach ($name in $DnsNames) { $san.AddDnsName($name) }
    foreach ($address in $IpAddresses) { $san.AddIpAddress([Net.IPAddress]::Parse($address)) }
    $request.CertificateExtensions.Add($san.Build())
    $serial = [Security.Cryptography.RandomNumberGenerator]::GetBytes(16)
    $serial[0] = $serial[0] -band 0x7f
    $leaf = $request.Create($ca, $now.AddMinutes(-5), $now.AddYears(2), $serial)
    $prefix = $Role.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $rdpTrustRoot "$prefix.crt"), $leaf.ExportCertificatePem(), $rdpEncoding)
    [IO.File]::WriteAllText((Join-Path $rdpTrustRoot "$prefix.key"), $rdpKey.ExportPkcs8PrivateKeyPem(), $rdpEncoding)
    [IO.File]::WriteAllText((Join-Path $rdpTrustRoot "$prefix-ca.pem"), $ca.ExportCertificatePem(), $rdpEncoding)
    [IO.File]::WriteAllBytes((Join-Path $rdpTrustRoot "$prefix-ca.der"), $ca.RawData)
    [IO.File]::WriteAllText((Join-Path $rdpTrustRoot "$prefix-ca.key"), $rdpCaKey.ExportPkcs8PrivateKeyPem(), $rdpEncoding)
    if ($Role -eq 'Console') {
        $master = [Security.Cryptography.RandomNumberGenerator]::GetBytes(32)
        [IO.File]::WriteAllBytes((Join-Path $rdpTrustRoot 'workspace-master.key'), $master)
        [Array]::Clear($master, 0, $master.Length)
    }
    [pscustomobject]@{ Role = $Role; Directory = $rdpTrustRoot; CertificateSha256 = $leaf.GetCertHashString([Security.Cryptography.HashAlgorithmName]::SHA256);
        ExpiresAt = $leaf.NotAfter.ToUniversalTime().ToString('o') }
} finally {
    if ($leaf) { $leaf.Dispose() }
    if ($ca) { $ca.Dispose() }
    $rdpKey.Dispose()
    $rdpCaKey.Dispose()
}
