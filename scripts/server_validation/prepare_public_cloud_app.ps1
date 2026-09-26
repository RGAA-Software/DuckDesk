#requires -Version 5.1

[CmdletBinding()]
param([string]$ComputerName = '39.71.45.66')

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$caPath = Join-Path $repositoryRoot '.env/public_single_server_console_ca.pem'
$credentialsOutput = Join-Path $repositoryRoot '.env/public_single_server_cloud_app_user.json'
if (-not (Test-Path -LiteralPath $caPath -PathType Leaf)) { throw 'Public Console CA is unavailable.' }
if (Test-Path -LiteralPath $credentialsOutput) { throw 'Refusing to overwrite test user credentials.' }
$machineText = Get-Content -LiteralPath (Join-Path $repositoryRoot '.env/test_machine.md') -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) { throw 'Public test host credential is incomplete.' }
$credential = [pscredential]::new("$machineName\Administrator", (ConvertTo-SecureString $password -AsPlainText -Force))
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporaryDirectory = Join-Path $temporaryRoot ('pixels-cloud-app-' + [guid]::NewGuid().ToString('N'))
$temporaryPasswordFile = Join-Path $temporaryDirectory 'admin-password'
$remoteSession = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $remoteSession = New-PSSession -ComputerName $ComputerName -Credential $credential
    $remoteHash = Invoke-Command -Session $remoteSession -ScriptBlock {
        (Get-FileHash -LiteralPath 'C:\ProgramData\Pixels\Server\config\console-ca.crt' -Algorithm SHA256).Hash
    }
    if ((Get-FileHash -LiteralPath $caPath -Algorithm SHA256).Hash -ne $remoteHash) {
        throw 'The local and installed Console CA differ.'
    }
    New-Item -ItemType Directory -Path $temporaryDirectory -ErrorAction Stop | Out-Null
    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    & icacls.exe $temporaryDirectory '/inheritance:r' '/grant:r' `
        "*${currentSid}:(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' *> $null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot protect temporary administrator credential.' }
    Copy-Item -LiteralPath 'C:\ProgramData\Pixels\Server\config\initial-admin-password' `
        -Destination $temporaryPasswordFile -FromSession $remoteSession -ErrorAction Stop
    & python.exe (Join-Path $PSScriptRoot 'prepare_public_cloud_app.py') --ca $caPath `
        --password-file $temporaryPasswordFile --credentials-output $credentialsOutput `
        --node-id '81612374-47bb-430f-8390-36dbddb82ce8'
    if ($LASTEXITCODE -ne 0) { throw 'Public cloud application preparation failed.' }
    & icacls.exe $credentialsOutput '/inheritance:r' '/grant:r' `
        "*${currentSid}:F" '*S-1-5-18:F' *> $null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot protect test user credentials.' }
} finally {
    if (Test-Path -LiteralPath $temporaryPasswordFile -PathType Leaf) {
        Remove-Item -LiteralPath $temporaryPasswordFile -Force
    }
    if (Test-Path -LiteralPath $temporaryDirectory -PathType Container) {
        Remove-Item -LiteralPath $temporaryDirectory -Force
    }
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
