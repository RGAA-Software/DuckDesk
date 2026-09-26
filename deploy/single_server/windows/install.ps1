#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$PackageRoot,
    [Parameter(Mandatory)] [string]$ExpectedManifestSha256,
    [Parameter(Mandatory)] [string]$ConfigRoot,
    [Parameter(Mandatory)] [string]$DataRoot,
    [string]$InstallRoot = "$env:ProgramFiles\Pixels\Server",
    [switch]$PreflightOnly
)

$ErrorActionPreference = 'Stop'

function Get-LowerHash {
    param([string]$Path)
    $hashAlgorithm = [Security.Cryptography.SHA256]::Create()
    $inputStream = [IO.File]::OpenRead($Path)
    try {
        return ([BitConverter]::ToString($hashAlgorithm.ComputeHash($inputStream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $inputStream.Dispose()
        $hashAlgorithm.Dispose()
    }
}

function Invoke-Sc {
    param([string[]]$Arguments)
    $nativeArguments = @($Arguments)
    $binaryPathIndex = [Array]::IndexOf($nativeArguments, 'binPath=')
    if ($binaryPathIndex -ge 0 -and $binaryPathIndex + 1 -lt $nativeArguments.Count) {
        $nativeArguments[$binaryPathIndex + 1] = $nativeArguments[$binaryPathIndex + 1].Replace('"', '\"')
    }
    $result = & sc.exe @nativeArguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "sc.exe $($Arguments[0]) failed: $($result -join ' ')" }
}

function Assert-PlainTree {
    param([string]$Root)
    foreach ($entry in @(Get-Item -LiteralPath $Root) + @(Get-ChildItem -LiteralPath $Root -Force -Recurse)) {
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse point is not allowed: $($entry.FullName)"
        }
    }
}

function Remove-ReleaseDirectory {
    param([string]$Path, [string]$Parent)
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    if ([IO.Path]::GetDirectoryName($resolvedPath) -cne [IO.Path]::GetFullPath($Parent)) {
        throw "Refusing release cleanup outside install root: $Path"
    }
    if (Test-Path -LiteralPath $resolvedPath) { Remove-Item -LiteralPath $resolvedPath -Recurse -Force }
}

function Protect-ServiceReadFile {
    param([string]$Path, [string[]]$ServiceNames)
    $administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $localSystem = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $servicePrincipals = @($ServiceNames | ForEach-Object {
        [Security.Principal.NTAccount]::new("NT SERVICE\$_").Translate([Security.Principal.SecurityIdentifier])
    })
    $security = [Security.AccessControl.FileSecurity]::new()
    $security.SetAccessRuleProtection($true, $false)
    $security.SetOwner($administrators)
    foreach ($identity in @($administrators, $localSystem) + $servicePrincipals) {
        $rights = if ($identity -in $servicePrincipals) { 'Read' } else { 'FullControl' }
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $identity, $rights, [Security.AccessControl.AccessControlType]::Allow
        )
        [void]$security.AddAccessRule($rule)
    }
    [IO.File]::SetAccessControl($Path, $security)
}

function Protect-ServiceDataDirectory {
    param([string]$Path, [string]$ServiceName)
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    if (-not [IO.Directory]::Exists($resolvedPath) -or
        -not $resolvedPath.StartsWith($script:ResolvedDataPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Service data directory must be an existing child of the persistent data root: $Path"
    }
    $administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $localSystem = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $servicePrincipal = [Security.Principal.NTAccount]::new("NT SERVICE\$ServiceName").Translate(
        [Security.Principal.SecurityIdentifier]
    )
    $security = [Security.AccessControl.DirectorySecurity]::new()
    $security.SetAccessRuleProtection($true, $false)
    $security.SetOwner($administrators)
    $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    foreach ($identity in @($administrators, $localSystem, $servicePrincipal)) {
        $rights = if ($identity -eq $servicePrincipal) { 'Modify' } else { 'FullControl' }
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $identity, $rights, $inheritance, [Security.AccessControl.PropagationFlags]::None,
            [Security.AccessControl.AccessControlType]::Allow
        )
        [void]$security.AddAccessRule($rule)
    }
    [IO.Directory]::SetAccessControl($resolvedPath, $security)
}

function Get-EnvironmentValue {
    param([string]$Path, [string]$Name)
    $prefix = "$Name="
    $matchingLines = @(Get-Content -LiteralPath $Path | Where-Object { $_.StartsWith($prefix, [StringComparison]::Ordinal) })
    if ($matchingLines.Count -ne 1) { throw "Environment field is missing or duplicated: $Name" }
    return $matchingLines[0].Substring($prefix.Length).Trim("'", '"')
}

function Get-PostgreSqlRootCertificatePath {
    param([string]$EnvironmentPath)
    $databaseUrl = Get-EnvironmentValue -Path $EnvironmentPath -Name 'PIXELS_CONSOLE_DATABASE_URL'
    $parsedUrl = [Uri]::new($databaseUrl)
    $matchingParameters = @($parsedUrl.Query.TrimStart('?').Split('&') |
        Where-Object { $_.StartsWith('sslrootcert=', [StringComparison]::Ordinal) })
    if ($matchingParameters.Count -ne 1) { throw 'Console database URL must contain one sslrootcert file.' }
    $encodedPath = $matchingParameters[0].Substring('sslrootcert='.Length)
    $certificatePath = [Uri]::UnescapeDataString($encodedPath)
    if (-not [IO.Path]::IsPathRooted($certificatePath)) { throw 'PostgreSQL root certificate path must be absolute.' }
    return $certificatePath
}

function Protect-ReferencedServiceFile {
    param([string]$Path, [string[]]$ServiceNames)
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    if (-not [IO.File]::Exists($resolvedPath) -or
        -not $resolvedPath.StartsWith($script:ResolvedConfigPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Service secret must be an existing file within the private configuration root: $Path"
    }
    Protect-ServiceReadFile -Path $resolvedPath -ServiceNames $ServiceNames
}

function Protect-LicenseDirectory {
    param([string]$LicensePath)
    $licenseDirectory = [IO.Path]::GetFullPath((Split-Path -Parent $LicensePath))
    if ($licenseDirectory -ieq $resolvedConfig -or
        -not $licenseDirectory.StartsWith($script:ResolvedConfigPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $licenseDirectory -PathType Container)) {
        throw 'The license must be placed in an existing dedicated child of the private configuration root.'
    }
    $otherEntries = @(Get-ChildItem -LiteralPath $licenseDirectory -Force | Where-Object {
        $_.FullName -ine [IO.Path]::GetFullPath($LicensePath)
    })
    if ($otherEntries.Count -ne 0) { throw 'The writable license directory must not contain other files.' }
    $administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $localSystem = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $servicePrincipal = [Security.Principal.NTAccount]::new('NT SERVICE\Pixels.Console').Translate(
        [Security.Principal.SecurityIdentifier]
    )
    $directorySecurity = [Security.AccessControl.DirectorySecurity]::new()
    $directorySecurity.SetAccessRuleProtection($true, $false)
    $directorySecurity.SetOwner($administrators)
    $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    foreach ($identity in @($administrators, $localSystem, $servicePrincipal)) {
        $rights = if ($identity -eq $servicePrincipal) { 'Modify' } else { 'FullControl' }
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $identity, $rights, $inheritance, [Security.AccessControl.PropagationFlags]::None,
            [Security.AccessControl.AccessControlType]::Allow
        )
        [void]$directorySecurity.AddAccessRule($rule)
    }
    [IO.Directory]::SetAccessControl($licenseDirectory, $directorySecurity)
    if (Test-Path -LiteralPath $LicensePath -PathType Leaf) {
        $fileSecurity = [Security.AccessControl.FileSecurity]::new()
        $fileSecurity.SetAccessRuleProtection($true, $false)
        $fileSecurity.SetOwner($administrators)
        foreach ($identity in @($administrators, $localSystem, $servicePrincipal)) {
            $rights = if ($identity -eq $servicePrincipal) { 'Modify' } else { 'FullControl' }
            $rule = [Security.AccessControl.FileSystemAccessRule]::new(
                $identity, $rights, [Security.AccessControl.AccessControlType]::Allow
            )
            [void]$fileSecurity.AddAccessRule($rule)
        }
        [IO.File]::SetAccessControl($LicensePath, $fileSecurity)
    }
}

if ($ExpectedManifestSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Expected manifest SHA-256 is invalid.' }
$resolvedPackage = (Resolve-Path -LiteralPath $PackageRoot).Path
$resolvedConfig = (Resolve-Path -LiteralPath $ConfigRoot).Path
$script:ResolvedConfigPrefix = $resolvedConfig.TrimEnd('\') + '\'
$resolvedData = (Resolve-Path -LiteralPath $DataRoot).Path
$script:ResolvedDataPrefix = $resolvedData.TrimEnd('\') + '\'
$resolvedInstall = [IO.Path]::GetFullPath($InstallRoot)
if (-not [IO.Path]::IsPathRooted($InstallRoot) -or $resolvedInstall -eq [IO.Path]::GetPathRoot($resolvedInstall)) {
    throw 'Install root is unsafe.'
}
Assert-PlainTree -Root $resolvedPackage
Assert-PlainTree -Root $resolvedConfig
if (((Get-Item -LiteralPath $resolvedData).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Persistent data root cannot be a reparse point.'
}

$manifestPath = Join-Path $resolvedPackage 'sha256.json'
if ((Get-LowerHash -Path $manifestPath) -cne $ExpectedManifestSha256) {
    throw 'Package manifest SHA-256 mismatch.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema_version -ne 1 -or $manifest.product -cne 'pixels-single-server' -or
    $manifest.distribution -cne 'customer' -or $manifest.platform -cne 'windows-x86_64' -or
    [string]$manifest.suite_version -cnotmatch '^\d+\.\d+\.\d+$') { throw 'Package identity is invalid.' }
$expectedPaths = @($manifest.files.PSObject.Properties.Name)
$actualPaths = @(Get-ChildItem -LiteralPath $resolvedPackage -File -Recurse | ForEach-Object {
    $_.FullName.Substring($resolvedPackage.TrimEnd('\').Length + 1).Replace('\', '/')
})
if ((@($actualPaths | Sort-Object) -join "`n") -cne (@($expectedPaths + 'sha256.json' | Sort-Object) -join "`n")) {
    throw 'Package files do not match the manifest.'
}
foreach ($relativePath in $expectedPaths) {
    if ($relativePath -notmatch '^[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$' -or $relativePath -match '(?i)(desk|auth)') {
        throw "Forbidden package path: $relativePath"
    }
    $actualHash = Get-LowerHash -Path (Join-Path $resolvedPackage $relativePath)
    if ($actualHash -cne [string]$manifest.files.$relativePath) { throw "Package file hash mismatch: $relativePath" }
}

$backupConfig = Get-Content -LiteralPath (Join-Path $resolvedConfig 'backup.json') -Raw | ConvertFrom-Json
if ($backupConfig.schema_version -ne 2 -or $backupConfig.plan.targets.Count -ne 3) {
    throw 'Backup configuration is not a three-target schema 2 plan.'
}
if ($null -ne $backupConfig.offsite_repository_root) {
    throw 'Single Server first version accepts a local Backup repository only.'
}
if (@($backupConfig.plan.targets | Where-Object { $_.state -eq 'required' }).Count -ne 1 -or
    [string]$backupConfig.plan.targets[0].database.service -cne 'console' -or
    [string]$backupConfig.plan.targets[1].service -cne 'auth' -or [string]$backupConfig.plan.targets[1].state -cne 'not_applicable' -or
    [string]$backupConfig.plan.targets[2].service -cne 'desk' -or [string]$backupConfig.plan.targets[2].state -cne 'not_applicable') {
    throw 'Backup plan must require only Console; Auth and Desk must be not_applicable.'
}
$deploymentId = [Guid]::Parse([string]$backupConfig.deployment_id)
if ($deploymentId -eq [Guid]::Empty) { throw 'Deployment ID is invalid.' }
$backupName = "Pixels.Backup.$($deploymentId.ToString('N').Substring(0, 12))"
$serviceNames = @('Pixels.Console', 'Pixels.Relay', $backupName)
$serviceWasRunning = @{}
$deploymentMarker = Join-Path $resolvedInstall 'deployment.id'
$currentPath = Join-Path $resolvedInstall 'current'
if (Test-Path -LiteralPath $deploymentMarker) {
    $installedDeployment = (Get-Content -LiteralPath $deploymentMarker -Raw).Trim()
    if ($installedDeployment -cne $deploymentId.ToString().ToLowerInvariant()) {
        throw 'Another Pixels Server deployment is installed here; uninstall it explicitly first.'
    }
} elseif (Test-Path -LiteralPath $currentPath) {
    throw 'An unrecognized Pixels Server installation exists here; uninstall it explicitly first.'
}
foreach ($environmentName in @('console.env', 'relay.env')) {
    $environmentPath = Join-Path $resolvedConfig $environmentName
    if (-not (Test-Path -LiteralPath $environmentPath -PathType Leaf)) { throw "Missing $environmentName" }
    $deploymentLine = @(Get-Content -LiteralPath $environmentPath | Where-Object { $_ -match '^PIXELS_DEPLOYMENT_ID=' })
    if ($deploymentLine.Count -ne 1 -or $deploymentLine[0] -cne "PIXELS_DEPLOYMENT_ID=$($deploymentId.ToString().ToLowerInvariant())") {
        throw "$environmentName deployment identity differs from Backup."
    }
    if (Select-String -LiteralPath $environmentPath -Pattern 'REPLACE' -Quiet) { throw "$environmentName still contains a placeholder." }
}
$otherBackup = @(Get-Service -Name 'Pixels.Backup.*' -ErrorAction SilentlyContinue |
    Where-Object Name -cne $backupName)
if ($otherBackup.Count -gt 0) { throw 'Another Pixels Backup deployment is installed; uninstall it explicitly first.' }
$previousCommands = @{}
$previousBackupEnvironment = $null
$hadBackupEnvironment = $false
foreach ($serviceName in $serviceNames) {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -ne $service) {
        $installedCommand = [string](Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$serviceName" -Name ImagePath).ImagePath
        if (-not (Test-Path -LiteralPath $currentPath) -or $installedCommand -notlike "*$(Join-Path $currentPath 'bin')*") {
            throw "Existing $serviceName does not belong to this install root; uninstall it explicitly first."
        }
        $previousCommands[$serviceName] = $installedCommand
        if ($serviceName -eq $backupName) {
            $environmentProperty = Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$backupName" `
                -Name Environment -ErrorAction SilentlyContinue
            if ($null -ne $environmentProperty) {
                $previousBackupEnvironment = @($environmentProperty.Environment)
                $hadBackupEnvironment = $true
            }
        }
    }
    $serviceWasRunning[$serviceName] = $null -ne $service -and $service.Status -ne 'Stopped'
}
if ($PreflightOnly) {
    Write-Output "PREFLIGHT_OK customer-server $($manifest.suite_version) deployment=$deploymentId"
    return
}

New-Item -ItemType Directory -Path $resolvedInstall -Force | Out-Null
$stagePath = Join-Path $resolvedInstall "stage-$([Guid]::NewGuid().ToString('N'))"
$previousPath = Join-Path $resolvedInstall "previous-$([Guid]::NewGuid().ToString('N'))"
$createdServices = [System.Collections.Generic.List[string]]::new()
$swapAttempted = $false
New-Item -ItemType Directory -Path $stagePath | Out-Null
try {
    foreach ($relativePath in $actualPaths) {
        $destinationPath = Join-Path $stagePath $relativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $resolvedPackage $relativePath) -Destination $destinationPath
    }
    foreach ($relativePath in $expectedPaths) {
        $stageHash = Get-LowerHash -Path (Join-Path $stagePath $relativePath)
        if ($stageHash -cne [string]$manifest.files.$relativePath) { throw "Staged file hash mismatch: $relativePath" }
    }
    foreach ($serviceName in $serviceNames) {
        $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
        if ($serviceWasRunning[$serviceName]) {
            Invoke-Sc -Arguments @('stop', $serviceName)
            $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
        }
    }
    $swapAttempted = $true
    if (Test-Path -LiteralPath $currentPath) { Move-Item -LiteralPath $currentPath -Destination $previousPath }
    Move-Item -LiteralPath $stagePath -Destination $currentPath
    $serviceCommands = @{
        'Pixels.Console' = "`"$(Join-Path $currentPath 'bin/px_console.exe')`" --service `"$(Join-Path $resolvedConfig 'console.env')`""
        'Pixels.Relay' = "`"$(Join-Path $currentPath 'bin/px_relay.exe')`" --service `"$(Join-Path $resolvedConfig 'relay.env')`""
        $backupName = "`"$(Join-Path $currentPath 'bin/px_backup.exe')`" service `"$(Join-Path $resolvedConfig 'backup.json')`""
    }
    foreach ($serviceName in $serviceNames) {
        $servicePrincipal = "NT SERVICE\$serviceName"
        if ($null -eq (Get-Service -Name $serviceName -ErrorAction SilentlyContinue)) {
            Invoke-Sc -Arguments @('create', $serviceName, 'binPath=', $serviceCommands[$serviceName], 'start=', 'auto',
                'obj=', $servicePrincipal, 'DisplayName=', $serviceName)
            $createdServices.Add($serviceName)
        } else {
            Invoke-Sc -Arguments @('config', $serviceName, 'binPath=', $serviceCommands[$serviceName], 'start=', 'auto', 'obj=', $servicePrincipal)
        }
        Invoke-Sc -Arguments @('sidtype', $serviceName, 'unrestricted')
        $programAclResult = & icacls.exe $resolvedInstall '/grant' "$($servicePrincipal):(OI)(CI)(RX)" 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Unable to grant service program access: $($programAclResult -join ' ')" }
        $aclResult = & icacls.exe $resolvedConfig '/grant' "$($servicePrincipal):(RX)" 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Unable to grant service directory traversal: $($aclResult -join ' ')" }
        if ($serviceName -ne 'Pixels.Relay') {
            $dataAclResult = & icacls.exe $resolvedData '/grant' "$($servicePrincipal):(RX)" 2>&1
            if ($LASTEXITCODE -ne 0) { throw "Unable to grant persistent data traversal: $($dataAclResult -join ' ')" }
        }
    }
    Protect-ServiceReadFile -Path (Join-Path $resolvedConfig 'console.env') -ServiceNames @('Pixels.Console')
    Protect-ServiceReadFile -Path (Join-Path $resolvedConfig 'relay.env') -ServiceNames @('Pixels.Relay')
    Protect-ServiceReadFile -Path (Join-Path $resolvedConfig 'backup.json') -ServiceNames @($backupName)
    $consoleEnvironment = Join-Path $resolvedConfig 'console.env'
    $postgresqlRootCertificate = Get-PostgreSqlRootCertificatePath -EnvironmentPath $consoleEnvironment
    $relayRootCertificate = Get-EnvironmentValue -Path (Join-Path $resolvedConfig 'relay.env') -Name 'PIXELS_RELAY_CONSOLE_CA_FILE'
    foreach ($fieldName in @('PIXELS_CONSOLE_TLS_CERT', 'PIXELS_CONSOLE_TLS_KEY', 'PIXELS_CONSOLE_GUEST_SOURCE_KEY',
            'PIXELS_CONSOLE_LICENSE_TRUST_STORE')) {
        Protect-ReferencedServiceFile -Path (Get-EnvironmentValue -Path $consoleEnvironment -Name $fieldName) -ServiceNames @('Pixels.Console')
    }
    Protect-LicenseDirectory -LicensePath (Get-EnvironmentValue -Path $consoleEnvironment -Name 'PIXELS_CONSOLE_LICENSE_FILE')
    if ([IO.Path]::GetFullPath($postgresqlRootCertificate) -ieq [IO.Path]::GetFullPath($relayRootCertificate)) {
        Protect-ReferencedServiceFile -Path $postgresqlRootCertificate -ServiceNames @('Pixels.Console', $backupName, 'Pixels.Relay')
    } else {
        Protect-ReferencedServiceFile -Path $postgresqlRootCertificate -ServiceNames @('Pixels.Console', $backupName)
        Protect-ReferencedServiceFile -Path $relayRootCertificate -ServiceNames @('Pixels.Relay')
    }
    $workspaceKeys = Get-EnvironmentValue -Path $consoleEnvironment -Name 'PIXELS_CONSOLE_WORKSPACE_KEYS' | ConvertFrom-Json
    foreach ($workspaceKey in $workspaceKeys) {
        Protect-ReferencedServiceFile -Path ([string]$workspaceKey.path) -ServiceNames @('Pixels.Console')
    }
    $cacheDirectory = Get-EnvironmentValue -Path $consoleEnvironment -Name 'PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY'
    Protect-ServiceDataDirectory -Path $cacheDirectory -ServiceName 'Pixels.Console'
    foreach ($propertyName in @('repository_root', 'scheduler_root', 'status_root')) {
        Protect-ServiceDataDirectory -Path ([string]$backupConfig.$propertyName) -ServiceName $backupName
    }
    foreach ($backupTarget in $backupConfig.plan.targets) {
        if ($backupTarget.state -eq 'required') {
            Protect-ReferencedServiceFile -Path ([string]$backupTarget.database.password_file) -ServiceNames @($backupName)
        }
    }
    New-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$backupName" -Name Environment `
        -PropertyType MultiString -Value @('PGSSLMODE=verify-full', "PGSSLROOTCERT=$postgresqlRootCertificate") -Force | Out-Null
    foreach ($serviceName in $serviceNames) {
        Invoke-Sc -Arguments @('start', $serviceName)
        (Get-Service -Name $serviceName).WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
    }
    Start-Sleep -Seconds 3
    foreach ($serviceName in $serviceNames) {
        if ((Get-Service -Name $serviceName).Status -ne 'Running') {
            throw "Service $serviceName did not remain running after startup."
        }
    }
    if (-not (Test-Path -LiteralPath $deploymentMarker)) {
        [IO.File]::WriteAllText($deploymentMarker, $deploymentId.ToString().ToLowerInvariant() + "`n")
    }
    if (Test-Path -LiteralPath $previousPath) {
        try { Remove-ReleaseDirectory -Path $previousPath -Parent $resolvedInstall }
        catch { Write-Warning "Previous program cleanup was deferred: $($_.Exception.Message)" }
    }
    Write-Output "RUNNING customer-server $($manifest.suite_version) deployment=$deploymentId"
} catch {
    $installError = $_
    if ($swapAttempted) {
        foreach ($serviceName in $serviceNames) {
            $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
            if ($null -ne $service -and $service.Status -ne 'Stopped') {
                & sc.exe stop $serviceName 2>&1 | Out-Null
                try { $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30)) } catch {}
            }
        }
        foreach ($createdService in $createdServices) { & sc.exe delete $createdService 2>&1 | Out-Null }
        if (Test-Path -LiteralPath $previousPath) {
            Remove-ReleaseDirectory -Path $currentPath -Parent $resolvedInstall
            Move-Item -LiteralPath $previousPath -Destination $currentPath
        } else {
            Remove-ReleaseDirectory -Path $currentPath -Parent $resolvedInstall
            if (Test-Path -LiteralPath $deploymentMarker) { Remove-Item -LiteralPath $deploymentMarker }
        }
        foreach ($serviceName in $previousCommands.Keys) {
            & sc.exe config $serviceName 'binPath=' $previousCommands[$serviceName] 2>&1 | Out-Null
        }
        if ($previousCommands.ContainsKey($backupName)) {
            if ($hadBackupEnvironment) {
                New-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$backupName" -Name Environment `
                    -PropertyType MultiString -Value $previousBackupEnvironment -Force | Out-Null
            } else {
                Remove-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$backupName" `
                    -Name Environment -ErrorAction SilentlyContinue
            }
        }
        foreach ($serviceName in $serviceNames) {
            if ($serviceWasRunning[$serviceName]) { & sc.exe start $serviceName 2>&1 | Out-Null }
        }
    } else {
        foreach ($serviceName in $serviceNames) {
            if ($serviceWasRunning[$serviceName]) { & sc.exe start $serviceName 2>&1 | Out-Null }
        }
    }
    throw $installError
} finally {
    if (Test-Path -LiteralPath $stagePath) { Remove-ReleaseDirectory -Path $stagePath -Parent $resolvedInstall }
}
