#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66',
    [string]$SuiteVersion = '1.0.4',
    [switch]$CoverInstall
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$setupPath = Join-Path $repositoryRoot `
    "build_official/private_server/customer/$SuiteVersion/windows/PixelsServer_${SuiteVersion}_Setup.exe"
$checksumPath = "$setupPath.sha256"
if (-not (Test-Path -LiteralPath $setupPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) {
    throw "Formal Windows Single Server $SuiteVersion Setup is unavailable."
}
$expectedHash = ((Get-Content -LiteralPath $checksumPath -Raw).Trim() -split '\s+')[0]
$actualHash = (Get-FileHash -LiteralPath $setupPath -Algorithm SHA256).Hash
if ($expectedHash -ne $actualHash) { throw 'Local Windows Setup SHA-256 differs.' }
$machineText = Get-Content -LiteralPath (Join-Path $repositoryRoot '.env/test_machine.md') -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) { throw 'Public test host credential is incomplete.' }
$credential = [pscredential]::new("$machineName\Administrator", (ConvertTo-SecureString $password -AsPlainText -Force))
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$remoteSession = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $remoteSession = New-PSSession -ComputerName $ComputerName -Credential $credential
    $remoteSetup = "D:\PixelsServer\staging\PixelsServer_${SuiteVersion}_Setup.exe"
    Invoke-Command -Session $remoteSession -ArgumentList $remoteSetup, $expectedHash, $CoverInstall.IsPresent -ScriptBlock {
        param($destination, $hash, $coverInstall)
        $existingInstallation = Test-Path -LiteralPath 'C:\ProgramData\Pixels\Server\config\setup.complete' -PathType Leaf
        if ($coverInstall -ne $existingInstallation) {
            throw 'Requested install mode does not match the remote Single Server state.'
        }
        if ((Get-Service -Name PixelsPostgreSQL18).Status -ne 'Running') {
            throw 'External PostgreSQL service is not running.'
        }
        $stagingDirectory = Split-Path $destination -Parent
        if (-not (Test-Path -LiteralPath $stagingDirectory -PathType Container)) {
            throw 'The reviewed remote staging directory is missing.'
        }
        if ((Test-Path -LiteralPath $destination -PathType Leaf) -and
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
            throw 'A different remote Setup already occupies the staging path.'
        }
    }
    $remoteAlreadyMatches = Invoke-Command -Session $remoteSession -ArgumentList $remoteSetup, $expectedHash `
        -ScriptBlock { param($destination, $hash) (Test-Path -LiteralPath $destination -PathType Leaf) -and
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $hash }
    if (-not $remoteAlreadyMatches) {
        Copy-Item -LiteralPath $setupPath -Destination $remoteSetup -ToSession $remoteSession -ErrorAction Stop
    }
    Invoke-Command -Session $remoteSession -ArgumentList $remoteSetup, $expectedHash -ScriptBlock {
        param($installer, $hash)
        if ((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash -ne $hash) {
            throw 'Remote Windows Setup SHA-256 differs.'
        }
        $installation = Start-Process -FilePath $installer -ArgumentList '/S' -WindowStyle Hidden -Wait -PassThru
        if ($installation.ExitCode -ne 0) { throw "Windows Setup failed with exit code $($installation.ExitCode)." }
        $serviceNames = if (Test-Path -LiteralPath 'C:\ProgramData\Pixels\Server\config\setup.complete') {
            @('Pixels.Console', 'Pixels.Relay', 'Pixels.Backup.*')
        } else { @('Pixels.Setup') }
        foreach ($serviceName in $serviceNames) {
            if ((Get-Service -Name $serviceName -ErrorAction Stop).Status -ne 'Running') {
                throw "$serviceName did not start."
            }
        }
        [pscustomobject]@{
            result = if ($serviceNames.Count -eq 3) { 'COVER_INSTALLED' } else { 'SETUP_READY' }
            setup_path = $installer
            setup_sha256 = $hash
            running_services = $serviceNames
        } | ConvertTo-Json -Compress
    }
} finally {
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
