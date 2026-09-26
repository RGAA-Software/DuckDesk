#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66',
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$licensePath = Join-Path $repositoryRoot '.env/pixels-pg-single-server-20260926-c4fdfa68/console.license'
$certificateAuthorityPath = Join-Path $repositoryRoot '.env/public_single_server_console_ca.pem'
$importScript = Join-Path $PSScriptRoot 'import_single_server_license.py'
if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf)) {
    throw 'The isolated CN Auth signed test license is unavailable.'
}
$machineText = Get-Content -LiteralPath (Join-Path $repositoryRoot '.env/test_machine.md') -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) { throw 'Public test host credential is incomplete.' }
$credential = [pscredential]::new("$machineName\Administrator", (ConvertTo-SecureString $password -AsPlainText -Force))
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$remoteSession = $null
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporaryDirectory = Join-Path $temporaryRoot ('pixels-single-license-' + [guid]::NewGuid().ToString('N'))
$temporaryPasswordFile = Join-Path $temporaryDirectory 'admin-password'
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $remoteSession = New-PSSession -ComputerName $ComputerName -Credential $credential
    $remoteCertificateAuthority = 'C:\ProgramData\Pixels\Server\config\console-ca.crt'
    $remoteCertificateHash = Invoke-Command -Session $remoteSession -ArgumentList $remoteCertificateAuthority `
        -ScriptBlock { param($certificatePath) (Get-FileHash -LiteralPath $certificatePath -Algorithm SHA256).Hash }
    if (Test-Path -LiteralPath $certificateAuthorityPath) {
        if ((Get-FileHash -LiteralPath $certificateAuthorityPath -Algorithm SHA256).Hash -ne $remoteCertificateHash) {
            throw 'Local Console CA differs from the remote deployment.'
        }
    } else {
        Copy-Item -LiteralPath $remoteCertificateAuthority -Destination $certificateAuthorityPath `
            -FromSession $remoteSession -ErrorAction Stop
    }
    if ((Get-FileHash -LiteralPath $certificateAuthorityPath -Algorithm SHA256).Hash -ne $remoteCertificateHash) {
        throw 'Downloaded Console CA hash differs.'
    }
    New-Item -ItemType Directory -Path $temporaryDirectory -ErrorAction Stop | Out-Null
    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    & icacls.exe $temporaryDirectory '/inheritance:r' '/grant:r' `
        "*${currentSid}:(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' *> $null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot protect the temporary administrator credential directory.' }
    Copy-Item -LiteralPath 'C:\ProgramData\Pixels\Server\config\initial-admin-password' `
        -Destination $temporaryPasswordFile -FromSession $remoteSession -ErrorAction Stop
    $pythonArguments = @($importScript, '--origin', "https://${ComputerName}:4600",
        '--ca', $certificateAuthorityPath, '--license', $licensePath,
        '--password-file', $temporaryPasswordFile)
    if ($VerifyOnly) { $pythonArguments += '--verify-only' }
    & python.exe @pythonArguments
    if ($LASTEXITCODE -ne 0) { throw 'Public Console license import failed.' }
} finally {
    $resolvedDirectory = [IO.Path]::GetFullPath($temporaryDirectory)
    if ($resolvedDirectory.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedDirectory) -like 'pixels-single-license-*') {
        if (Test-Path -LiteralPath $temporaryPasswordFile -PathType Leaf) {
            Remove-Item -LiteralPath $temporaryPasswordFile -Force
        }
        if (Test-Path -LiteralPath $resolvedDirectory -PathType Container) {
            Remove-Item -LiteralPath $resolvedDirectory -Force
        }
    }
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
