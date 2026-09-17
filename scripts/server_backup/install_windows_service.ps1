#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$PackageRoot,
    [Parameter(Mandatory)] [string]$ExpectedPackageManifestSha256,
    [Parameter(Mandatory)] [string]$ConfigPath,
    [string]$InstallationRoot = "$env:ProgramFiles\Pixels\Backup",
    [string]$DataRoot = "$env:ProgramData\Pixels"
)

$ErrorActionPreference = 'Stop'

function Resolve-RequiredFile {
    param([string]$Path, [string]$Description)
    if (-not [IO.Path]::IsPathFullyQualified($Path)) { throw "$Description must be an absolute path." }
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    if (-not [IO.File]::Exists($resolvedPath) -or $resolvedPath.Contains('"')) { throw "$Description is invalid: $resolvedPath" }
    return $resolvedPath
}

function Assert-PlainDirectoryTree {
    param([string]$Root)
    foreach ($item in @(Get-Item -LiteralPath $Root) + @(Get-ChildItem -LiteralPath $Root -Force -Recurse)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Package contains a reparse point: $($item.FullName)" }
    }
}

function Invoke-ServiceControl {
    param([Parameter(ValueFromRemainingArguments)][string[]]$Arguments)
    $output = & sc.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "sc.exe $($Arguments[0]) failed: $($output -join ' ')" }
    return $output
}

function Wait-ServiceRunning {
    param([string]$Name)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $service = Get-Service -Name $Name -ErrorAction Stop
        if ($service.Status -eq 'Running') { return }
        if ($service.Status -eq 'Stopped') {
            $query = & sc.exe queryex $Name 2>&1
            throw "Backup service stopped during startup: $($query -join ' ')"
        }
        if ([DateTime]::UtcNow -ge $deadline) {
            $query = & sc.exe queryex $Name 2>&1
            throw "Backup service startup timed out: $($query -join ' ')"
        }
        Start-Sleep -Milliseconds 100
    } while ($true)
}

function Set-ServiceAcl {
    param(
        [string]$Path,
        [Security.AccessControl.FileSystemRights]$ServiceRights,
        [switch]$Directory
    )
    $administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $localSystem = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $serviceIdentity = [Security.Principal.NTAccount]::new($script:ServicePrincipal).Translate(
        [Security.Principal.SecurityIdentifier]
    )
    if ($Directory) {
        $security = [Security.AccessControl.DirectorySecurity]::new()
        $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    } else {
        $security = [Security.AccessControl.FileSecurity]::new()
        $inheritance = [Security.AccessControl.InheritanceFlags]::None
    }
    $security.SetAccessRuleProtection($true, $false)
    $security.SetOwner($administrators)
    foreach ($rule in @(
        [Security.AccessControl.FileSystemAccessRule]::new($localSystem, 'FullControl', $inheritance,
            [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow),
        [Security.AccessControl.FileSystemAccessRule]::new($administrators, 'FullControl', $inheritance,
            [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow),
        [Security.AccessControl.FileSystemAccessRule]::new($serviceIdentity, $ServiceRights, $inheritance,
            [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow)
    )) {
        [void]$security.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $security
}

function Grant-ServiceTraversal {
    param([string]$Path)
    $parentPath = [IO.Path]::GetDirectoryName($Path)
    if ([string]::IsNullOrWhiteSpace($parentPath)) { throw "Backup path requires an explicit parent: $Path" }
    $traversalResult = & icacls.exe $parentPath '/grant' "$($script:ServicePrincipal):(RX)" 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Unable to grant service traversal on $parentPath`: $($traversalResult -join ' ')" }
}

function Protect-ServiceDirectory {
    param([string]$Path)
    if (-not [IO.Path]::IsPathFullyQualified($Path) -or -not [IO.Directory]::Exists($Path)) {
        throw "Backup directory must already exist and be absolute: $Path"
    }
    Grant-ServiceTraversal -Path $Path
    Set-ServiceAcl -Path $Path -ServiceRights Modify -Directory
}

function Protect-ServiceReadFile {
    param([string]$Path)
    $resolvedPath = Resolve-RequiredFile -Path $Path -Description 'Private service file'
    $parentPath = [IO.Path]::GetDirectoryName($resolvedPath)
    Grant-ServiceTraversal -Path $parentPath
    Set-ServiceAcl -Path $parentPath -ServiceRights ReadAndExecute -Directory
    Set-ServiceAcl -Path $resolvedPath -ServiceRights Read
}

function Protect-ServiceRelease {
    param([string]$Path)
    Set-ServiceAcl -Path $Path -ServiceRights ReadAndExecute -Directory
}

if ($ExpectedPackageManifestSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Expected package-manifest SHA-256 must be lowercase hex.' }
$resolvedPackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
if (-not [IO.Directory]::Exists($resolvedPackageRoot)) { throw 'Package root is unavailable.' }
Assert-PlainDirectoryTree -Root $resolvedPackageRoot
$sourceManifestPath = Resolve-RequiredFile -Path (Join-Path $resolvedPackageRoot 'package-manifest.json') -Description 'Package manifest'
if ((Get-FileHash -LiteralPath $sourceManifestPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $ExpectedPackageManifestSha256) {
    throw 'Package manifest is not the expected reviewed release.'
}
$packageManifest = Get-Content -LiteralPath $sourceManifestPath -Raw | ConvertFrom-Json
if ($packageManifest.schema_version -ne 1 -or $packageManifest.product -ne 'pixels-backup-windows-x64' -or
    $packageManifest.package_id -notmatch '^[a-zA-Z0-9.-]{1,96}$' -or $packageManifest.postgresql_version -ne '18.6') {
    throw 'Package manifest contract is invalid.'
}
$expectedFiles = @{}
foreach ($file in $packageManifest.files) {
    $relativePath = [string]$file.path
    if ($relativePath -notmatch '^[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+){0,3}$' -or $expectedFiles.ContainsKey($relativePath) -or
        [string]$file.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Package file manifest is invalid.' }
    $expectedFiles[$relativePath] = $file
}
$actualFiles = @(Get-ChildItem -LiteralPath $resolvedPackageRoot -File -Recurse | ForEach-Object {
    [IO.Path]::GetRelativePath($resolvedPackageRoot, $_.FullName).Replace('\', '/')
})
$expectedPaths = @($expectedFiles.Keys) + 'package-manifest.json'
if ((@($actualFiles | Sort-Object) -join "`n") -cne (@($expectedPaths | Sort-Object) -join "`n")) {
    throw 'Package contains missing or unlisted files.'
}
foreach ($entry in $expectedFiles.GetEnumerator()) {
    $sourcePath = Resolve-RequiredFile -Path (Join-Path $resolvedPackageRoot $entry.Key) -Description 'Package file'
    if ((Get-Item -LiteralPath $sourcePath).Length -ne [long]$entry.Value.size -or
        (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$entry.Value.sha256) {
        throw "Package file identity mismatch: $($entry.Key)"
    }
}

$resolvedInputConfig = Resolve-RequiredFile -Path $ConfigPath -Description 'Backup configuration'
$configuration = Get-Content -LiteralPath $resolvedInputConfig -Raw | ConvertFrom-Json
if ($configuration.schema_version -ne 2) { throw 'Backup configuration schema is invalid.' }
$deploymentId = [Guid]::Parse([string]$configuration.deployment_id)
if ($deploymentId -eq [Guid]::Empty) { throw 'Backup deployment identity cannot be empty.' }
$deploymentShortId = $deploymentId.ToString('N').Substring(0, 12)
$serviceName = "Pixels.Backup.$deploymentShortId"
$script:ServicePrincipal = "NT SERVICE\$serviceName"

$resolvedInstallationRoot = [IO.Path]::GetFullPath($InstallationRoot)
$resolvedDataRoot = [IO.Path]::GetFullPath($DataRoot)
$releaseParent = Join-Path $resolvedInstallationRoot 'releases'
$releasePath = Join-Path $releaseParent ([string]$packageManifest.package_id)
New-Item -ItemType Directory -Path $releaseParent -Force | Out-Null
if (-not (Test-Path -LiteralPath $releasePath)) {
    $stagingPath = Join-Path $releaseParent ".staging-$([Guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $stagingPath | Out-Null
    try {
        foreach ($relativePath in $actualFiles) {
            $destinationPath = Join-Path $stagingPath $relativePath
            New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $resolvedPackageRoot $relativePath) -Destination $destinationPath
        }
        Move-Item -LiteralPath $stagingPath -Destination $releasePath
    } catch {
        if (Test-Path -LiteralPath $stagingPath) {
            $resolvedStagingPath = [IO.Path]::GetFullPath($stagingPath)
            if ([IO.Path]::GetDirectoryName($resolvedStagingPath) -cne [IO.Path]::GetFullPath($releaseParent)) { throw 'Refusing unsafe staging cleanup.' }
            Remove-Item -LiteralPath $resolvedStagingPath -Recurse -Force
        }
        throw
    }
}

$installedManifestPath = Join-Path $releasePath 'package-manifest.json'
if ((Get-FileHash -LiteralPath $installedManifestPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $ExpectedPackageManifestSha256) {
    throw 'Installed release does not match the reviewed package.'
}
Assert-PlainDirectoryTree -Root $releasePath
$installedFiles = @(Get-ChildItem -LiteralPath $releasePath -File -Recurse | ForEach-Object {
    [IO.Path]::GetRelativePath($releasePath, $_.FullName).Replace('\', '/')
})
if ((@($installedFiles | Sort-Object) -join "`n") -cne (@($expectedPaths | Sort-Object) -join "`n")) {
    throw 'Installed release contains missing or unlisted files.'
}
foreach ($entry in $expectedFiles.GetEnumerator()) {
    $installedFile = Resolve-RequiredFile -Path (Join-Path $releasePath $entry.Key) -Description 'Installed package file'
    if ((Get-Item -LiteralPath $installedFile).Length -ne [long]$entry.Value.size -or
        (Get-FileHash -LiteralPath $installedFile -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$entry.Value.sha256) {
        throw "Installed package file identity mismatch: $($entry.Key)"
    }
}
$installedBinary = Join-Path $releasePath 'px_backup.exe'
$installedPgDump = Join-Path $releasePath 'postgresql/bin/pg_dump.exe'
$installedPgRestore = Join-Path $releasePath 'postgresql/bin/pg_restore.exe'
$configuration.pg_dump_path = $installedPgDump
$configuration.pg_dump_sha256 = (Get-FileHash -LiteralPath $installedPgDump -Algorithm SHA256).Hash.ToLowerInvariant()
$configuration.pg_restore_path = $installedPgRestore
$configuration.pg_restore_sha256 = (Get-FileHash -LiteralPath $installedPgRestore -Algorithm SHA256).Hash.ToLowerInvariant()

$deploymentDataRoot = Join-Path $resolvedDataRoot "$deploymentId\backup"
New-Item -ItemType Directory -Path $deploymentDataRoot -Force | Out-Null
$installedConfigPath = Join-Path $deploymentDataRoot 'config.json'
$nextConfigPath = Join-Path $deploymentDataRoot "config.next-$([Guid]::NewGuid().ToString('N')).json"
[IO.File]::WriteAllText($nextConfigPath, ($configuration | ConvertTo-Json -Depth 20 -Compress), [Text.UTF8Encoding]::new($false))

$existingService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
$serviceWasRunning = $null -ne $existingService -and $existingService.Status -ne 'Stopped'
$previousImagePath = if ($null -ne $existingService) {
    (Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$serviceName" -Name ImagePath).ImagePath
} else { $null }
$previousConfigBytes = if (Test-Path -LiteralPath $installedConfigPath) { [IO.File]::ReadAllBytes($installedConfigPath) } else { $null }
$serviceCreated = $false

try {
    if ($null -ne $existingService -and $existingService.Status -ne 'Stopped') {
        Invoke-ServiceControl stop $serviceName | Out-Null
        $existingService.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
    }
    $serviceCommand = "`"$installedBinary`" service `"$installedConfigPath`""
    if ($null -eq $existingService) {
        Invoke-ServiceControl create $serviceName 'binPath=' $serviceCommand 'start=' 'auto' 'obj=' $script:ServicePrincipal `
            'DisplayName=' 'Pixels PostgreSQL Backup' | Out-Null
        $serviceCreated = $true
    } else {
        Invoke-ServiceControl config $serviceName 'binPath=' $serviceCommand 'start=' 'auto' 'obj=' $script:ServicePrincipal `
            'DisplayName=' 'Pixels PostgreSQL Backup' | Out-Null
    }
    Protect-ServiceRelease -Path $releasePath
    foreach ($directoryProperty in @('repository_root', 'scheduler_root', 'status_root')) {
        Protect-ServiceDirectory -Path ([string]$configuration.$directoryProperty)
    }
    if ($null -ne $configuration.offsite_repository_root) { Protect-ServiceDirectory -Path ([string]$configuration.offsite_repository_root) }
    foreach ($backupTarget in $configuration.plan.targets) {
        if ([string]$backupTarget.state -eq 'required') { Protect-ServiceReadFile -Path ([string]$backupTarget.database.password_file) }
    }
    if (Test-Path -LiteralPath $installedConfigPath) {
        Move-Item -LiteralPath $installedConfigPath -Destination (Join-Path $deploymentDataRoot 'config.previous.json') -Force
    }
    Move-Item -LiteralPath $nextConfigPath -Destination $installedConfigPath
    Protect-ServiceReadFile -Path $installedConfigPath

    Invoke-ServiceControl description $serviceName "Pixels PostgreSQL backup executor for deployment $deploymentId" | Out-Null
    Invoke-ServiceControl failure $serviceName 'reset=' '600' 'actions=' 'restart/3000/restart/10000/""/0' | Out-Null
    Invoke-ServiceControl sidtype $serviceName unrestricted | Out-Null
    Invoke-ServiceControl start $serviceName | Out-Null
    Wait-ServiceRunning -Name $serviceName
    Start-Sleep -Seconds 3
    if ((Get-Service -Name $serviceName).Status -ne 'Running') { throw 'Backup service did not remain running after startup validation.' }
    Write-Output "RUNNING $serviceName package=$($packageManifest.package_id) config=$installedConfigPath"
} catch {
    $installError = $_
    $currentService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -ne $currentService -and $currentService.Status -ne 'Stopped') {
        & sc.exe stop $serviceName 2>&1 | Out-Null
        try { $currentService.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30)) } catch {}
    }
    if ($serviceCreated) {
        & sc.exe delete $serviceName 2>&1 | Out-Null
    } elseif ($null -ne $previousImagePath) {
        & sc.exe config $serviceName 'binPath=' $previousImagePath 2>&1 | Out-Null
    }
    if ($null -ne $previousConfigBytes) {
        [IO.File]::WriteAllBytes($installedConfigPath, $previousConfigBytes)
        Protect-ServiceReadFile -Path $installedConfigPath
    } elseif (Test-Path -LiteralPath $installedConfigPath) {
        Remove-Item -LiteralPath $installedConfigPath -Force
    }
    if (-not $serviceCreated -and $serviceWasRunning) { & sc.exe start $serviceName 2>&1 | Out-Null }
    throw $installError
} finally {
    if (Test-Path -LiteralPath $nextConfigPath) { Remove-Item -LiteralPath $nextConfigPath -Force }
}
