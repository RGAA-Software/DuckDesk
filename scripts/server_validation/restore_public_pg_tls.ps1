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
    Invoke-Command -Session $remoteSession -ScriptBlock {
        $retiredTlsDirectory = 'D:\PixelsServer\retired-runtime-20260926\app\tls'
        $activeTlsDirectory = 'D:\PixelsServer\app\tls'
        $postgresConfiguration = 'D:\PixelsServer\data\postgresql\postgresql.conf'
        $postgresPasswordFile = 'D:\PixelsServer\config\pg-admin-password'
        $postgresClient = 'D:\PixelsServer\postgresql\pgsql\bin\psql.exe'
        if (-not (Test-Path -LiteralPath $retiredTlsDirectory -PathType Container) -or
            -not (Test-Path -LiteralPath $postgresConfiguration -PathType Leaf) -or
            -not (Test-Path -LiteralPath $postgresPasswordFile -PathType Leaf) -or
            -not (Test-Path -LiteralPath $postgresClient -PathType Leaf)) {
            throw 'PostgreSQL TLS restoration preflight failed; no files were copied.'
        }
        $expectedFiles = @('console.crt', 'console.key', 'console-ca.pem')
        foreach ($fileName in $expectedFiles) {
            if (-not (Test-Path -LiteralPath (Join-Path $retiredTlsDirectory $fileName) -PathType Leaf)) {
                throw "Archived PostgreSQL TLS input is missing: $fileName"
            }
        }
        $configurationText = Get-Content -LiteralPath $postgresConfiguration -Raw
        if ($configurationText -notmatch "(?m)^ssl = on\s*$" -or
            $configurationText -notmatch "(?m)^ssl_cert_file = 'D:/PixelsServer/app/tls/console.crt'\s*$" -or
            $configurationText -notmatch "(?m)^ssl_key_file = 'D:/PixelsServer/app/tls/console.key'\s*$") {
            throw 'PostgreSQL TLS configuration differs from the verified archived-file path.'
        }
        $otherPixelsServices = @(Get-Service -Name 'Pixels.Console', 'Pixels.Relay', 'Pixels.Backup.*' `
            -ErrorAction SilentlyContinue | Where-Object Status -eq Running)
        if ($otherPixelsServices.Count -ne 0) { throw 'Pixels business services are running; refusing PostgreSQL restart.' }
        $certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2(
            (Join-Path $retiredTlsDirectory 'console.crt'))
        if ($certificate.NotAfter.ToUniversalTime() -le [datetime]::UtcNow) {
            throw 'Archived PostgreSQL TLS certificate has expired.'
        }
        if (-not (Test-Path -LiteralPath $activeTlsDirectory)) {
            New-Item -ItemType Directory -Path $activeTlsDirectory -ErrorAction Stop | Out-Null
        }
        foreach ($fileName in $expectedFiles) {
            $source = Join-Path $retiredTlsDirectory $fileName
            $destination = Join-Path $activeTlsDirectory $fileName
            if (-not (Test-Path -LiteralPath $destination)) {
                Copy-Item -LiteralPath $source -Destination $destination -ErrorAction Stop
            }
            if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {
                throw "Restored PostgreSQL TLS file hash differs: $fileName"
            }
        }
        $retiredAppDirectory = Split-Path $retiredTlsDirectory -Parent
        $activeAppDirectory = Split-Path $activeTlsDirectory -Parent
        foreach ($directoryPair in @(@($retiredAppDirectory, $activeAppDirectory),
                @($retiredTlsDirectory, $activeTlsDirectory))) {
            Set-Acl -LiteralPath $directoryPair[1] -AclObject (Get-Acl -LiteralPath $directoryPair[0])
        }
        foreach ($fileName in $expectedFiles) {
            Set-Acl -LiteralPath (Join-Path $activeTlsDirectory $fileName) -AclObject `
                (Get-Acl -LiteralPath (Join-Path $retiredTlsDirectory $fileName))
        }
        $postgresService = Get-Service -Name PixelsPostgreSQL18
        if ($postgresService.Status -eq 'Running') {
            Restart-Service -Name PixelsPostgreSQL18 -ErrorAction Stop
        } else {
            Start-Service -Name PixelsPostgreSQL18 -ErrorAction Stop
        }
        $service = Get-Service -Name PixelsPostgreSQL18
        $service.WaitForStatus('Running', [timespan]::FromSeconds(30))
        try {
            $env:PGPASSWORD = (Get-Content -LiteralPath $postgresPasswordFile -Raw).Trim()
            $env:PGSSLMODE = 'verify-full'
            $env:PGSSLROOTCERT = Join-Path $activeTlsDirectory 'console-ca.pem'
            $databaseNames = @(& $postgresClient -X -A -t -h localhost -p 54329 -U postgres -d postgres `
                -c 'SELECT datname FROM pg_database WHERE datistemplate = false ORDER BY datname')
            if ($LASTEXITCODE -ne 0) { throw 'PostgreSQL TLS database query failed.' }
        } finally {
            Remove-Item Env:PGPASSWORD -ErrorAction SilentlyContinue
            Remove-Item Env:PGSSLMODE -ErrorAction SilentlyContinue
            Remove-Item Env:PGSSLROOTCERT -ErrorAction SilentlyContinue
        }
        [pscustomobject]@{
            result = 'POSTGRES_TLS_RESTORED'
            databases = @($databaseNames | Where-Object { $_ })
        } | ConvertTo-Json -Compress
    }
} finally {
    if ($remoteSession) { Remove-PSSession $remoteSession }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
}
