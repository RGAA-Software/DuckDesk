#requires -Version 5.1

[CmdletBinding()]
param([string]$ComputerName = '39.71.45.66')

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
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
    Invoke-Command -Session $remoteSession -ArgumentList $ComputerName -ScriptBlock {
        param($publicHost)
        $configRoot = 'C:\ProgramData\Pixels\Server\config'
        $administratorPasswordFile = Join-Path $configRoot 'initial-admin-password'
        $postgresPasswordFile = 'D:\PixelsServer\config\pg-admin-password'
        $postgresCertificateAuthority = 'D:\PixelsServer\app\tls\console-ca.pem'
        if ((Get-Service -Name Pixels.Setup -ErrorAction Stop).Status -ne 'Running' -or
            (Test-Path -LiteralPath (Join-Path $configRoot 'setup.complete')) -or
            -not (Test-Path -LiteralPath $postgresPasswordFile -PathType Leaf) -or
            -not (Test-Path -LiteralPath $postgresCertificateAuthority -PathType Leaf)) {
            throw 'Single Server setup is not in the expected first-run state.'
        }
        if (-not (Test-Path -LiteralPath $administratorPasswordFile -PathType Leaf)) {
            $randomBytes = New-Object byte[] 24
            $randomGenerator = [Security.Cryptography.RandomNumberGenerator]::Create()
            try { $randomGenerator.GetBytes($randomBytes) } finally { $randomGenerator.Dispose() }
            $administratorPassword = ([BitConverter]::ToString($randomBytes)).Replace('-', '').ToLowerInvariant()
            $fileStream = [IO.FileStream]::new($administratorPasswordFile,
                [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try {
                $passwordBytes = [Text.Encoding]::UTF8.GetBytes($administratorPassword)
                $fileStream.Write($passwordBytes, 0, $passwordBytes.Length)
                $fileStream.Flush($true)
            } finally { $fileStream.Dispose() }
        }
        $initialPassword = (Get-Content -LiteralPath $administratorPasswordFile -Raw).Trim()
        $request = @{
            postgresql_host = 'localhost'
            postgresql_port = 54329
            postgresql_administrator = 'postgres'
            postgresql_password = (Get-Content -LiteralPath $postgresPasswordFile -Raw).Trim()
            postgresql_ca_pem = [string](Get-Content -LiteralPath $postgresCertificateAuthority -Raw)
            public_host = $publicHost
            initial_username = 'admin'
            initial_password = $initialPassword
        }
        try {
            $setupResult = Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:4700/initialize' `
                -Headers @{ Origin = 'http://127.0.0.1:4700' } -ContentType 'application/json' `
                -Body ($request | ConvertTo-Json -Compress) -TimeoutSec 120
        } catch {
            $response = $_.Exception.Response
            $statusCode = if ($response) { [int]$response.StatusCode } else { 0 }
            $responseBody = if ($_.ErrorDetails.Message) {
                [string]$_.ErrorDetails.Message
            } elseif ($response) {
                $reader = New-Object IO.StreamReader($response.GetResponseStream())
                try { $reader.ReadToEnd() } finally { $reader.Dispose() }
            } else { $_.Exception.Message }
            throw "Single Server initialization failed: HTTP $statusCode $responseBody"
        } finally {
            $request.postgresql_password = $null
            $request.initial_password = $null
        }
        if (-not $setupResult.deployment_id -or -not $setupResult.console_origin -or
            -not (Test-Path -LiteralPath (Join-Path $configRoot 'setup.complete'))) {
            throw 'Single Server initialization response or completion marker is missing.'
        }
        $serviceRecords = @(Get-Service -Name 'Pixels.Console', 'Pixels.Relay', 'Pixels.Backup.*' `
            -ErrorAction SilentlyContinue | Select-Object Name, Status)
        [pscustomobject]@{
            result = 'INITIALIZED'
            deployment_id = [string]$setupResult.deployment_id
            console_origin = [string]$setupResult.console_origin
            administrator_username = 'admin'
            administrator_password_file = $administratorPasswordFile
            services = $serviceRecords
        } | ConvertTo-Json -Depth 4 -Compress
    }
} finally {
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
