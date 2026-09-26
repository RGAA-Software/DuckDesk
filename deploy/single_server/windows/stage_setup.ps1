#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$PackageRoot,
    [Parameter(Mandatory)] [string]$ExpectedManifestSha256,
    [Parameter(Mandatory)] [string]$ConfigRoot,
    [Parameter(Mandatory)] [string]$DataRoot,
    [Parameter(Mandatory)] [string]$InstallRoot
)

$ErrorActionPreference = 'Stop'
function Get-Hash([string]$Path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}
function Protect-Directory([string]$Path) {
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    if (((Get-Item -LiteralPath $resolvedPath).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Directory cannot be a reparse point: $resolvedPath"
    }
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    $administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $system = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $acl.SetOwner($administrators)
    foreach ($identity in @($administrators, $system)) {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $identity, 'FullControl', 'ContainerInherit, ObjectInherit', 'None', 'Allow'
        )
        [void]$acl.AddAccessRule($rule)
    }
    [IO.Directory]::SetAccessControl($resolvedPath, $acl)
}

if ($ExpectedManifestSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid manifest SHA-256.' }
$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$manifestPath = Join-Path $package 'sha256.json'
if ((Get-Hash $manifestPath) -cne $ExpectedManifestSha256) { throw 'Package manifest differs.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema_version -ne 1 -or $manifest.product -cne 'pixels-single-server' -or
    $manifest.distribution -cne 'customer' -or $manifest.platform -cne 'windows-x86_64') {
    throw 'Package identity differs.'
}
$expectedPaths = @($manifest.files.PSObject.Properties.Name)
$actualPaths = @(Get-ChildItem -LiteralPath $package -File -Recurse | ForEach-Object {
    if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Package contains a reparse point.' }
    $_.FullName.Substring($package.TrimEnd('\').Length + 1).Replace('\', '/')
})
if ((@($actualPaths | Sort-Object) -join "`n") -cne (@($expectedPaths + 'sha256.json' | Sort-Object) -join "`n")) {
    throw 'Package file list differs.'
}
foreach ($relativePath in $expectedPaths) {
    if ($relativePath -notmatch '^[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$' -or
        (Get-Hash (Join-Path $package $relativePath)) -cne [string]$manifest.files.$relativePath) {
        throw "Package file differs: $relativePath"
    }
}
$install = [IO.Path]::GetFullPath($InstallRoot)
$config = [IO.Path]::GetFullPath($ConfigRoot)
$data = [IO.Path]::GetFullPath($DataRoot)
foreach ($target in @($install, $config, $data)) {
    if ($target -eq [IO.Path]::GetPathRoot($target)) { throw 'A root directory cannot be used.' }
}
if (Test-Path -LiteralPath (Join-Path $config 'setup.complete')) {
    throw 'A completed deployment must use the overwrite installer path.'
}
$staged = Join-Path $install 'setup_payload'
if (Test-Path -LiteralPath $staged) {
    $stagedManifest = Join-Path $staged 'sha256.json'
    if (-not (Test-Path -LiteralPath $stagedManifest -PathType Leaf) -or
        (Get-Hash $stagedManifest) -cne $ExpectedManifestSha256) {
        throw 'The staged setup differs from this package.'
    }
    foreach ($relativePath in $expectedPaths) {
        $stagedFile = Join-Path $staged $relativePath
        if (-not (Test-Path -LiteralPath $stagedFile -PathType Leaf) -or
            (Get-Hash $stagedFile) -cne [string]$manifest.files.$relativePath) {
            throw "Staged setup file differs: $relativePath"
        }
    }
    $service = Get-Service -Name 'Pixels.Setup' -ErrorAction SilentlyContinue
    $expectedExecutable = Join-Path $staged 'bin/px_console_admin.exe'
    $installedCommand = [string](Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\Pixels.Setup' `
        -Name ImagePath -ErrorAction Stop).ImagePath
    if ($null -eq $service -or
        -not $installedCommand.StartsWith("`"$expectedExecutable`" setup-service ",
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The existing Pixels Setup service belongs to another installation.'
    }
    if ($service.Status -ne 'Running') {
        $serviceStartup = & sc.exe start Pixels.Setup 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Cannot restart Pixels Setup service: $($serviceStartup -join ' ')" }
    }
    Write-Output 'SETUP_READY http://127.0.0.1:4700/'
    return
}
if ((Test-Path -LiteralPath (Join-Path $install 'current')) -or
    (Test-Path -LiteralPath (Join-Path $config 'console.env'))) {
    throw 'An incomplete deployment has no matching staged setup; inspect it before reinstalling.'
}
Protect-Directory $config
Protect-Directory $data
Protect-Directory $install
New-Item -ItemType Directory -Path $staged | Out-Null
foreach ($relativePath in $actualPaths) {
    $destination = Join-Path $staged $relativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $package $relativePath) -Destination $destination
}
if ((Get-Hash (Join-Path $staged 'sha256.json')) -cne $ExpectedManifestSha256) {
    throw 'Staged package manifest differs.'
}
$executable = Join-Path $staged 'bin/px_console_admin.exe'
$command = "`"$executable`" setup-service `"$config`" `"$data`" `"$(Join-Path $install 'current')`" `"$staged`" $ExpectedManifestSha256"
$nativeCommand = $command.Replace('"', '\"')
if (Get-Service -Name 'Pixels.Setup' -ErrorAction SilentlyContinue) {
    throw 'A Pixels Setup service is already installed.'
}
$serviceCreation = & sc.exe create Pixels.Setup binPath= $nativeCommand start= demand obj= LocalSystem DisplayName= 'Pixels Server Setup' 2>&1
if ($LASTEXITCODE -ne 0) { throw "Cannot create Pixels Setup service: $($serviceCreation -join ' ')" }
$serviceStartup = & sc.exe start Pixels.Setup 2>&1
if ($LASTEXITCODE -ne 0) { throw "Cannot start Pixels Setup service: $($serviceStartup -join ' ')" }
Write-Output 'SETUP_READY http://127.0.0.1:4700/'
