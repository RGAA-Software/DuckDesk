#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$BinaryPath,
    [Parameter(Mandatory)]
    [string]$ConfigPath
)

$ErrorActionPreference = 'Stop'

function Resolve-RequiredFile {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$Description
    )
    if (-not [IO.Path]::IsPathFullyQualified($Path)) {
        throw "$Description must be an absolute path."
    }
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    if (-not [IO.File]::Exists($resolvedPath)) {
        throw "$Description does not exist: $resolvedPath"
    }
    if ($resolvedPath.Contains('"')) {
        throw "$Description cannot contain a quotation mark."
    }
    return $resolvedPath
}

function Invoke-ServiceControl {
    param([Parameter(ValueFromRemainingArguments)][string[]]$Arguments)
    $output = & sc.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "sc.exe $($Arguments[0]) failed: $($output -join ' ')"
    }
    return $output
}

function Set-BackupTrustedOwner {
    param([Parameter(Mandatory)][string]$Path)
    $ownerResult = & icacls.exe $Path '/setowner' '*S-1-5-32-544' 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to assign the Administrators owner to $Path`: $($ownerResult -join ' ')"
    }
}

function Remove-LegacySharedServiceGrant {
    param([Parameter(Mandatory)][string]$Path)
    $removeResult = & icacls.exe $Path '/remove:g' '*S-1-5-19' 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to remove the legacy LocalService grant from $Path`: $($removeResult -join ' ')"
    }
}

function Grant-BackupServiceTraversal {
    param([Parameter(Mandatory)][string]$Path)
    $parentPath = [IO.Path]::GetDirectoryName($Path)
    if ([string]::IsNullOrWhiteSpace($parentPath)) {
        throw "Backup path requires an explicit parent: $Path"
    }
    $serviceTraversalGrant = "$($script:BackupServicePrincipal):(RX)"
    $traversalResult = & icacls.exe $parentPath '/grant' $serviceTraversalGrant 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to grant service traversal on $parentPath`: $($traversalResult -join ' ')"
    }
}

function Protect-DirectoryForBackupService {
    param([Parameter(Mandatory)][string]$Path)
    if (-not [IO.Path]::IsPathFullyQualified($Path) -or -not [IO.Directory]::Exists($Path)) {
        throw "Backup directory must already exist and be absolute: $Path"
    }
    Grant-BackupServiceTraversal -Path $Path
    $serviceModifyGrant = "$($script:BackupServicePrincipal):(OI)(CI)M"
    $accessResult = & icacls.exe $Path '/inheritance:r' '/grant:r' `
        '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' $serviceModifyGrant 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to protect backup directory $Path`: $($accessResult -join ' ')"
    }
    Remove-LegacySharedServiceGrant -Path $Path
    Set-BackupTrustedOwner -Path $Path
}

function Protect-PrivateReadFileForBackupService {
    param([Parameter(Mandatory)][string]$Path)
    $resolvedPath = Resolve-RequiredFile -Path $Path -Description 'Backup credential file'
    $credentialParent = [IO.Path]::GetDirectoryName($resolvedPath)
    Grant-BackupServiceTraversal -Path $credentialParent
    $serviceDirectoryReadGrant = "$($script:BackupServicePrincipal):(OI)(CI)RX"
    $parentResult = & icacls.exe $credentialParent '/inheritance:r' '/grant:r' `
        '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' $serviceDirectoryReadGrant 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to protect credential directory: $($parentResult -join ' ')"
    }
    Remove-LegacySharedServiceGrant -Path $credentialParent
    Set-BackupTrustedOwner -Path $credentialParent
    $serviceFileReadGrant = "$($script:BackupServicePrincipal):R"
    $fileResult = & icacls.exe $resolvedPath '/inheritance:r' '/grant:r' `
        '*S-1-5-18:F' '*S-1-5-32-544:F' $serviceFileReadGrant 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to protect credential file: $($fileResult -join ' ')"
    }
    Remove-LegacySharedServiceGrant -Path $resolvedPath
    Set-BackupTrustedOwner -Path $resolvedPath
}

$resolvedBinaryPath = Resolve-RequiredFile -Path $BinaryPath -Description 'Backup executable'
$resolvedConfigPath = Resolve-RequiredFile -Path $ConfigPath -Description 'Backup configuration'
$configuration = Get-Content -LiteralPath $resolvedConfigPath -Raw | ConvertFrom-Json
$deploymentId = [Guid]::Parse([string]$configuration.deployment_id)
$deploymentShortId = $deploymentId.ToString('N').Substring(0, 12)
$serviceName = "Pixels.Backup.$deploymentShortId"
$script:BackupServicePrincipal = "NT SERVICE\$serviceName"

$serviceCommand = "`"$resolvedBinaryPath`" service `"$resolvedConfigPath`""
$existingService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($null -eq $existingService) {
    Invoke-ServiceControl create $serviceName 'binPath=' $serviceCommand 'start=' 'auto' `
        'obj=' $script:BackupServicePrincipal 'DisplayName=' 'Pixels PostgreSQL Backup' | Out-Null
} else {
    if ($existingService.Status -ne 'Stopped') {
        Invoke-ServiceControl stop $serviceName | Out-Null
        $existingService.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
    }
    Invoke-ServiceControl config $serviceName 'binPath=' $serviceCommand 'start=' 'auto' `
        'obj=' $script:BackupServicePrincipal 'DisplayName=' 'Pixels PostgreSQL Backup' | Out-Null
}
Invoke-ServiceControl description $serviceName `
    "Pixels independent PostgreSQL backup executor for deployment $deploymentId" | Out-Null
Invoke-ServiceControl failure $serviceName 'reset=' '600' 'actions=' 'restart/3000/restart/10000/""/0' | Out-Null
Invoke-ServiceControl sidtype $serviceName unrestricted | Out-Null

foreach ($directoryProperty in @('repository_root', 'scheduler_root', 'status_root')) {
    Protect-DirectoryForBackupService -Path ([string]$configuration.$directoryProperty)
}
foreach ($backupTarget in $configuration.plan.targets) {
    if ([string]$backupTarget.state -eq 'required') {
        Protect-PrivateReadFileForBackupService -Path ([string]$backupTarget.database.password_file)
    }
}
$configParent = [IO.Path]::GetDirectoryName($resolvedConfigPath)
Grant-BackupServiceTraversal -Path $configParent
$serviceDirectoryReadGrant = "$($script:BackupServicePrincipal):(OI)(CI)RX"
$parentAccess = & icacls.exe $configParent '/inheritance:r' '/grant:r' `
    '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' $serviceDirectoryReadGrant 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Unable to protect configuration directory: $($parentAccess -join ' ')"
}
Remove-LegacySharedServiceGrant -Path $configParent
Set-BackupTrustedOwner -Path $configParent
$serviceFileReadGrant = "$($script:BackupServicePrincipal):R"
$configAccess = & icacls.exe $resolvedConfigPath '/inheritance:r' '/grant:r' `
    '*S-1-5-18:F' '*S-1-5-32-544:F' $serviceFileReadGrant 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Unable to protect configuration file: $($configAccess -join ' ')"
}
Remove-LegacySharedServiceGrant -Path $resolvedConfigPath
Set-BackupTrustedOwner -Path $resolvedConfigPath
Invoke-ServiceControl start $serviceName | Out-Null
(Get-Service -Name $serviceName).WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
Write-Output "RUNNING $serviceName"
