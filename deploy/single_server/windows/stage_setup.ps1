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

function Get-LowerHash([string]$Path) {
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
    $security = [Security.AccessControl.DirectorySecurity]::new()
    $security.SetAccessRuleProtection($true, $false)
    $administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $localSystem = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $security.SetOwner($administrators)
    foreach ($identity in @($administrators, $localSystem)) {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $identity, 'FullControl', 'ContainerInherit, ObjectInherit', 'None', 'Allow'
        )
        [void]$security.AddAccessRule($rule)
    }
    [IO.Directory]::SetAccessControl($resolvedPath, $security)
}

if ($ExpectedManifestSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid manifest SHA-256.' }
$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$manifestPath = Join-Path $package 'sha256.json'
if ((Get-LowerHash $manifestPath) -cne $ExpectedManifestSha256) { throw 'Package manifest differs.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema_version -ne 1 -or $manifest.product -cne 'pixels-single-server' -or
    $manifest.distribution -cne 'official' -or $manifest.platform -cne 'windows-x86_64') {
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
        (Get-LowerHash (Join-Path $package $relativePath)) -cne [string]$manifest.files.$relativePath) {
        throw "Package file differs: $relativePath"
    }
}
$install = [IO.Path]::GetFullPath($InstallRoot)
$config = [IO.Path]::GetFullPath($ConfigRoot)
$data = [IO.Path]::GetFullPath($DataRoot)
foreach ($target in @($install, $config, $data)) {
    if (-not [IO.Path]::IsPathRooted($target) -or $target -eq [IO.Path]::GetPathRoot($target)) {
        throw 'A root directory cannot be used.'
    }
}
if (Test-Path -LiteralPath (Join-Path $config 'setup.complete') -PathType Leaf) {
    throw 'A completed deployment must use the overwrite installer path.'
}
Protect-Directory $config
Protect-Directory $data
Protect-Directory $install
$administrator = Join-Path $package 'bin/px_console_admin.exe'
& $administrator setup-server $config $data (Join-Path $install 'current') $package windows $ExpectedManifestSha256
if ($LASTEXITCODE -ne 0) { throw 'Automatic Single Server initialization failed; configuration and PostgreSQL were retained.' }
if (-not (Test-Path -LiteralPath (Join-Path $config 'setup.complete') -PathType Leaf)) {
    throw 'Automatic Single Server initialization did not finish.'
}
Write-Output 'SETUP_READY'
