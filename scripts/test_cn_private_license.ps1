#requires -Version 7.0

[CmdletBinding()]
param(
    [string]$ConsoleAdminPath = '.cache/pg-cargo/release/px_console_admin.exe',
    [string]$PublicTestHost = '39.71.45.66',
    [switch]$Issue,
    [guid]$DeploymentId,
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$authConfiguration = Get-Content (Join-Path $repository '.env/auth_rgaa_vip.json') -Raw | ConvertFrom-Json
$machineText = Get-Content (Join-Path $repository '.env/test_machine.md') -Raw -Encoding UTF8
$consoleAdmin = [IO.Path]::GetFullPath((Join-Path $repository $ConsoleAdminPath))
if ($OutputDirectory -and (-not $Issue -or $DeploymentId -eq [guid]::Empty)) {
    throw 'Exporting a test license requires -Issue and an explicit deployment ID.'
}
if ($OutputDirectory -and -not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    throw 'The isolated output directory must already exist.'
}
if (-not (Test-Path -LiteralPath $consoleAdmin -PathType Leaf)) {
    throw 'Focused Release px_console_admin is missing.'
}
$machinePassword = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineUsername = [regex]::Match($machineText, '(?m)^\s*-\s*\u7528\u6237\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
if (-not $machinePassword -or -not $machineName -or -not $machineUsername) {
    throw 'Public test host credential is incomplete.'
}
$qualifiedUsername = if ($machineUsername.Contains('\')) { $machineUsername } else { "$machineName\$machineUsername" }
$credential = [pscredential]::new($qualifiedUsername, (ConvertTo-SecureString $machinePassword -AsPlainText -Force))
$authOrigin = ([Uri]$authConfiguration.url).GetLeftPart([UriPartial]::Authority)
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporaryDirectory = Join-Path $temporaryRoot ('pixels-cn-license-' + [guid]::NewGuid().ToString('N'))
$authToken = ''
$remoteSession = $null
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $PublicTestHost -Force
    $remoteSession = New-PSSession -ComputerName $PublicTestHost -Credential $credential
    $installedMaterial = Invoke-Command -Session $remoteSession -ScriptBlock {
        [pscustomobject]@{
            trust_bytes = [IO.File]::ReadAllBytes('D:\PixelsServer\secrets\console\auth-trust-store.json')
            license_bytes = [IO.File]::ReadAllBytes('D:\PixelsServer\secrets\console\license.pxlic')
            deployment_id = [string]((Get-Content 'D:\PixelsServer\config\stack-secrets.json' -Raw | ConvertFrom-Json).DeploymentId)
        }
    }
    $trustBytes = [byte[]]$installedMaterial.trust_bytes
    $trustStore = [Text.Encoding]::UTF8.GetString([byte[]]$trustBytes) | ConvertFrom-Json
    if ($trustStore.schema_version -ne 2 -or $trustStore.trusted_keys.Count -ne 1) {
        throw 'The installed Auth public trust store is not the expected minimal PXLIC2 form.'
    }

    [void](New-Item -ItemType Directory -Path $temporaryDirectory)
    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    & icacls.exe $temporaryDirectory '/inheritance:r' '/grant:r' "*${currentSid}:(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' *> $null
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not make the isolated license directory private.'
    }
    $trustPath = Join-Path $temporaryDirectory 'auth-trust-store.json'
    $licensePath = Join-Path $temporaryDirectory 'console.license'
    [IO.File]::WriteAllBytes($trustPath, $trustBytes)
    [IO.File]::WriteAllBytes($licensePath, [byte[]]$installedMaterial.license_bytes)
    $env:PIXELS_DEPLOYMENT_ID = [string]$installedMaterial.deployment_id
    $env:PIXELS_CONSOLE_LICENSE_TRUST_STORE = $trustPath
    $env:PIXELS_CONSOLE_LICENSE_FILE = $licensePath
    $preflightOutput = & $consoleAdmin validate-license 2>&1
    if ($LASTEXITCODE -ne 0 -or $preflightOutput -notmatch 'Console license validated') {
        throw "Existing public test license preflight failed; no new license was issued: $preflightOutput"
    }
    if (-not $Issue) {
        [pscustomobject]@{ Result = 'PASS'; ExistingLicensePreflight = $true }
        return
    }
    $authHealth = Invoke-WebRequest -Uri "$authOrigin/health/ready"
    $clockDifference = ([DateTimeOffset]::Parse($authHealth.Headers.Date) - [DateTimeOffset]::UtcNow).TotalSeconds
    if ($authHealth.StatusCode -ne 204 -or [math]::Abs($clockDifference) -gt 30) {
        throw 'CN Auth is not ready or its clock differs by more than 30 seconds; no license was issued.'
    }

    $login = Invoke-RestMethod -Method Post -Uri "$authOrigin/api/auth/sessions" -ContentType 'application/json' -Body (@{
        username = $authConfiguration.username
        password = $authConfiguration.password
    } | ConvertTo-Json)
    $authToken = [string]$login.token
    $authHeaders = @{ Authorization = "Bearer $authToken" }
    $customers = Invoke-RestMethod -Uri "$authOrigin/api/auth/customers?limit=100" -Headers $authHeaders
    $testCustomers = @($customers | Where-Object { $_.name -eq 'Pixels Official public validation' })
    if ($testCustomers.Count -ne 1) {
        throw 'The existing CN Auth validation customer was not uniquely identified; no license was issued.'
    }

    $deploymentId = if ($DeploymentId -eq [guid]::Empty) { [guid]::NewGuid().ToString() } else { $DeploymentId.ToString() }
    $issueResponse = Invoke-RestMethod -Method Post -Uri "$authOrigin/api/auth/licenses/issue" -Headers $authHeaders `
        -ContentType 'application/json' -Body (@{
            request_id = [guid]::NewGuid().ToString()
            request = @{
                operation = 'create'
                terms = @{
                    customer_id = [string]$testCustomers[0].id
                    deployment_id = $deploymentId
                    expires_at = [DateTimeOffset]::UtcNow.AddHours(2).ToUnixTimeSeconds()
                    max_streams = 1
                    services = @('cloud_applications')
                }
            }
        } | ConvertTo-Json -Depth 6)
    if (-not [string]$issueResponse.wire -or -not [string]$issueResponse.license_id) {
        throw 'CN Auth did not return a signed PXLIC2 wire.'
    }
    $wireParts = ([string]$issueResponse.wire).Split('.')
    if ($wireParts.Count -ne 3 -or $wireParts[0] -ne 'PXLIC2') {
        throw 'CN Auth returned an unexpected license wire.'
    }
    $encodedPayload = $wireParts[1].Replace('-', '+').Replace('_', '/')
    $encodedPayload = $encodedPayload.PadRight($encodedPayload.Length + ((4 - $encodedPayload.Length % 4) % 4), '=')
    $licensePayload = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($encodedPayload)) | ConvertFrom-Json
    if ($licensePayload.deployment_id -ne $deploymentId -or $licensePayload.key_id -ne $trustStore.active_key_id -or
        $licensePayload.max_streams -ne 1 -or @($licensePayload.services).Count -ne 1 -or
        $licensePayload.services[0] -ne 'cloud_applications') {
        throw 'CN Auth license payload did not match the isolated Customer request.'
    }
    $secondsUntilValid = [long]$licensePayload.issued_at - [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    if ($secondsUntilValid -gt 30) {
        throw 'CN Auth license issued_at is too far ahead of the local clock.'
    }
    if ($secondsUntilValid -ge 0) {
        Start-Sleep -Seconds ($secondsUntilValid + 1)
    }

    [IO.File]::WriteAllText($licensePath, [string]$issueResponse.wire, [Text.UTF8Encoding]::new($false))
    $env:PIXELS_DEPLOYMENT_ID = $deploymentId
    $env:PIXELS_CONSOLE_LICENSE_TRUST_STORE = $trustPath
    $env:PIXELS_CONSOLE_LICENSE_FILE = $licensePath
    $validationOutput = & $consoleAdmin validate-license 2>&1
    if ($LASTEXITCODE -ne 0 -or $validationOutput -notmatch 'Console license validated') {
        throw "Customer Console rejected the CN Auth signed license: $validationOutput"
    }
    $env:PIXELS_DEPLOYMENT_ID = [guid]::NewGuid().ToString()
    & $consoleAdmin validate-license *> $null
    if ($LASTEXITCODE -eq 0) {
        throw 'Customer Console accepted a signed license for another deployment.'
    }
    if ($OutputDirectory) {
        $resolvedOutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
        $isolatedEnvironmentRoot = [IO.Path]::GetFullPath((Join-Path $repository '.env'))
        if (-not $resolvedOutputDirectory.StartsWith($isolatedEnvironmentRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            -not ([IO.Path]::GetFileName($resolvedOutputDirectory) -like 'pixels-pg-*')) {
            throw 'Test license export is restricted to an isolated PostgreSQL test directory.'
        }
        [IO.File]::WriteAllBytes((Join-Path $resolvedOutputDirectory 'auth-trust-store.json'), $trustBytes)
        [IO.File]::WriteAllText((Join-Path $resolvedOutputDirectory 'console.license'), [string]$issueResponse.wire, [Text.UTF8Encoding]::new($false))
    }
    [pscustomobject]@{
        Result = 'PASS'
        CustomerId = [string]$testCustomers[0].id
        DeploymentId = $deploymentId
        LicenseId = [string]$issueResponse.license_id
        Revision = [int]$issueResponse.revision
        MaxStreams = 1
        Services = 'cloud_applications'
        ConsoleAdminHash = (Get-FileHash -LiteralPath $consoleAdmin -Algorithm SHA256).Hash
    }
} finally {
    foreach ($environmentName in @('PIXELS_DEPLOYMENT_ID', 'PIXELS_CONSOLE_LICENSE_TRUST_STORE', 'PIXELS_CONSOLE_LICENSE_FILE')) {
        Remove-Item "Env:$environmentName" -ErrorAction SilentlyContinue
    }
    if ($authToken) {
        try {
            Invoke-RestMethod -Method Delete -Uri "$authOrigin/api/auth/session" -Headers @{Authorization="Bearer $authToken"} | Out-Null
        } catch {
            Write-Warning 'CN Auth acceptance login cleanup failed.'
        }
    }
    if ($remoteSession) {
        Remove-PSSession $remoteSession
    }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
    $machinePassword = $null
    $authConfiguration.password = $null
    if (Test-Path -LiteralPath $temporaryDirectory -PathType Container) {
        $resolvedDirectory = [IO.Path]::GetFullPath($temporaryDirectory)
        if (-not $resolvedDirectory.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not ([IO.Path]::GetFileName($resolvedDirectory) -like 'pixels-cn-license-*')) {
            throw 'CN Auth test cleanup path is outside the isolated temporary directory.'
        }
        Remove-Item -LiteralPath $resolvedDirectory -Recurse -Force
    }
}
