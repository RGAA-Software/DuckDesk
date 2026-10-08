$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$terminalServer = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server'
if ($terminalServer.fDenyTSConnections -ne 0) { throw 'RDP host connections are disabled.' }
$rdpBinding = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RDP-Tcp'
$certificateHash = $rdpBinding.SSLCertificateSHA1Hash
if ($certificateHash) {
    $thumbprint = if ($certificateHash -is [byte[]]) { [BitConverter]::ToString($certificateHash).Replace('-', '') } else { ([string]$certificateHash).Replace(' ', '') }
    $certificates = @(Get-ChildItem -LiteralPath 'Cert:\LocalMachine\Remote Desktop', 'Cert:\LocalMachine\My' | Where-Object { $_.Thumbprint -eq $thumbprint })
} else {
    $certificates = @(Get-ChildItem -LiteralPath 'Cert:\LocalMachine\Remote Desktop')
}
if ($certificates.Count -ne 1) { throw 'The active Windows RDP certificate cannot be uniquely identified.' }
$certificate = $certificates[0]
if ((Get-Date) -lt $certificate.NotBefore -or (Get-Date) -ge $certificate.NotAfter) { throw 'The Windows RDP certificate is outside its validity period.' }
$digest = [Security.Cryptography.SHA256]::Create()
try { $targetPin = [BitConverter]::ToString($digest.ComputeHash($certificate.RawData)).Replace('-', '').ToLowerInvariant() }
finally { $digest.Dispose() }
[Console]::WriteLine(([ordered]@{ target_domain = $env:COMPUTERNAME; target_certificate_sha256 = $targetPin } | ConvertTo-Json -Compress))
