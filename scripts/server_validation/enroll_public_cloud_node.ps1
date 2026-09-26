#requires -Version 5.1

[CmdletBinding()]
param([string]$ComputerName = '39.71.45.66')

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$caPath = Join-Path $repositoryRoot '.env/public_single_server_console_ca.pem'
if (-not (Test-Path -LiteralPath $caPath -PathType Leaf)) { throw 'Public Console CA is unavailable.' }
$machineText = Get-Content -LiteralPath (Join-Path $repositoryRoot '.env/test_machine.md') -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $password -or -not $machineName) { throw 'Public test host credential is incomplete.' }
$credential = [pscredential]::new("$machineName\Administrator", (ConvertTo-SecureString $password -AsPlainText -Force))
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporaryDirectory = Join-Path $temporaryRoot ('pixels-node-enrollment-' + [guid]::NewGuid().ToString('N'))
$temporaryPasswordFile = Join-Path $temporaryDirectory 'admin-password'
$temporaryTokenFile = Join-Path $temporaryDirectory 'node-token'
$remoteSession = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $remoteSession = New-PSSession -ComputerName $ComputerName -Credential $credential
    $remoteCaPath = 'C:\ProgramData\Pixels\Server\config\console-ca.crt'
    $remoteHash = Invoke-Command -Session $remoteSession -ArgumentList $remoteCaPath -ScriptBlock {
        param($certificatePath) (Get-FileHash -LiteralPath $certificatePath -Algorithm SHA256).Hash
    }
    if ((Get-FileHash -LiteralPath $caPath -Algorithm SHA256).Hash -ne $remoteHash) {
        throw 'The local and installed Console CA differ.'
    }
    New-Item -ItemType Directory -Path $temporaryDirectory -ErrorAction Stop | Out-Null
    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    & icacls.exe $temporaryDirectory '/inheritance:r' '/grant:r' `
        "*${currentSid}:(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' *> $null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot protect temporary enrollment files.' }
    Copy-Item -LiteralPath 'C:\ProgramData\Pixels\Server\config\initial-admin-password' `
        -Destination $temporaryPasswordFile -FromSession $remoteSession -ErrorAction Stop
    & python.exe (Join-Path $PSScriptRoot 'enroll_public_cloud_node.py') --ca $caPath `
        --password-file $temporaryPasswordFile --token-output $temporaryTokenFile
    if ($LASTEXITCODE -ne 0) { throw 'Console node enrollment failed.' }
    $nodeToken = (Get-Content -LiteralPath $temporaryTokenFile -Raw).Trim()
    if ($nodeToken -notmatch '^[0-9a-f]{64}$') { throw 'Invalid one-time node token.' }
    Invoke-Command -Session $remoteSession -ArgumentList $remoteCaPath, $nodeToken, $ComputerName -ScriptBlock {
        param($certificatePath, $nodeToken, $publicHost)
        $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certificatePath)
        $rootStore = [Security.Cryptography.X509Certificates.X509Store]::new('Root', 'LocalMachine')
        $rootStore.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        try {
            if (-not $rootStore.Certificates.Find('FindByThumbprint', $certificate.Thumbprint, $false).Count) {
                $rootStore.Add($certificate)
            }
        } finally { $rootStore.Close() }
        $configuration = [ordered]@{
            schema_version = 4
            endpoint = "wss://${publicHost}:4600/api/console/node-control"
            node_token = $nodeToken
            public_host = $publicHost
        } | ConvertTo-Json -Compress
        $serviceExecutable = 'C:\Program Files\Pixels Cloud Node\px_service.exe'
        $process = [Diagnostics.Process]::new()
        $process.StartInfo.FileName = $serviceExecutable
        $process.StartInfo.Arguments = '--configure-node-control'
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.RedirectStandardInput = $true
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        $process.StartInfo.CreateNoWindow = $true
        try {
            if (-not $process.Start()) { throw 'Node configuration process did not start.' }
            $process.StandardInput.WriteLine($configuration)
            $process.StandardInput.Close()
            $output = $process.StandardOutput.ReadToEnd()
            $errorText = $process.StandardError.ReadToEnd()
            $process.WaitForExit(15000) | Out-Null
            if ($process.ExitCode -ne 0) { throw "Node configuration failed: $errorText" }
            Restart-Service -Name px_service -ErrorAction Stop
            (Get-Service -Name px_service).WaitForStatus('Running', [timespan]::FromSeconds(30))
            [pscustomobject]@{ result = 'NODE_CONFIGURED'; service = 'Running'; output = $output.Trim() } |
                ConvertTo-Json -Compress
        } finally { $process.Dispose() }
    }
} finally {
    foreach ($secretFile in @($temporaryPasswordFile, $temporaryTokenFile)) {
        if (Test-Path -LiteralPath $secretFile -PathType Leaf) { Remove-Item -LiteralPath $secretFile -Force }
    }
    if (Test-Path -LiteralPath $temporaryDirectory -PathType Container) {
        Remove-Item -LiteralPath $temporaryDirectory -Force
    }
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
