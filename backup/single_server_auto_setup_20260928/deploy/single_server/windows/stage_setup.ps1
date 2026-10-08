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
function Wait-SetupPage {
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            $request = [Net.WebRequest]::Create('http://127.0.0.1:4700/')
            $request.Proxy = $null
            $request.Timeout = 1000
            $response = $request.GetResponse()
            try {
                if ([int]$response.StatusCode -eq 200) { return }
            } finally {
                $response.Dispose()
            }
        } catch {
            Start-Sleep -Milliseconds 500
        }
    }
    throw 'Pixels Setup service did not make its local page available.'
}
function Ensure-SetupFirewallRule([string]$ExecutablePath) {
    if (-not (Get-NetFirewallRule -Name 'Pixels.Server.Setup.LAN' -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -Name 'Pixels.Server.Setup.LAN' -DisplayName 'Pixels Server Setup (LAN)' `
            -Direction Inbound -Action Allow -Protocol TCP -LocalPort 4700 -Program $ExecutablePath `
            -RemoteAddress LocalSubnet -Profile Any | Out-Null
    }
}
function Invoke-Sc([string[]]$Arguments) {
    $result = & sc.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "sc.exe $($Arguments[0]) failed: $($result -join ' ')" }
}
function Stop-SetupService {
    $service = Get-Service -Name 'Pixels.Setup' -ErrorAction Stop
    if ($service.Status -ne 'Stopped') {
        Invoke-Sc -Arguments @('stop', 'Pixels.Setup')
        $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
    }
}
function Assert-StagedPackage([string]$StageRoot, [string]$ManifestHash) {
    if (((Get-Item -LiteralPath $StageRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The existing staged setup is a reparse point.'
    }
    $manifestPath = Join-Path $StageRoot 'sha256.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
        (Get-Hash $manifestPath) -cne $ManifestHash) {
        throw 'The existing staged setup manifest is invalid.'
    }
    $stagedManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($stagedManifest.schema_version -ne 1 -or $stagedManifest.product -cne 'pixels-single-server' -or
        $stagedManifest.distribution -cne 'official' -or $stagedManifest.platform -cne 'windows-x86_64') {
        throw 'The existing staged setup identity differs.'
    }
    $expectedFiles = @($stagedManifest.files.PSObject.Properties.Name)
    $actualFiles = @(Get-ChildItem -LiteralPath $StageRoot -File -Recurse | ForEach-Object {
        if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'The existing staged setup contains a reparse point.'
        }
        $_.FullName.Substring($StageRoot.TrimEnd('\').Length + 1).Replace('\', '/')
    })
    if ((@($actualFiles | Sort-Object) -join "`n") -cne (@($expectedFiles + 'sha256.json' | Sort-Object) -join "`n")) {
        throw 'The existing staged setup file list differs.'
    }
    foreach ($relativePath in $expectedFiles) {
        if ($relativePath -notmatch '^[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$' -or
            (Get-Hash (Join-Path $StageRoot $relativePath)) -cne [string]$stagedManifest.files.$relativePath) {
            throw "The existing staged setup file differs: $relativePath"
        }
    }
}

if ($ExpectedManifestSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid manifest SHA-256.' }
$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$manifestPath = Join-Path $package 'sha256.json'
if ((Get-Hash $manifestPath) -cne $ExpectedManifestSha256) { throw 'Package manifest differs.' }
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
    $service = Get-Service -Name 'Pixels.Setup' -ErrorAction SilentlyContinue
    $expectedExecutable = Join-Path $staged 'bin/px_console_admin.exe'
    $installedCommand = [string](Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\Pixels.Setup' `
        -Name ImagePath -ErrorAction Stop).ImagePath
    $expectedCommandPrefix = "`"$expectedExecutable`" setup-service `"$config`" `"$data`" `"$(Join-Path $install 'current')`" `"$staged`" "
    if ($null -eq $service -or
        -not $installedCommand.StartsWith($expectedCommandPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The existing Pixels Setup service belongs to another installation.'
    }
    $previousManifestHash = $installedCommand.Substring($expectedCommandPrefix.Length)
    if ($previousManifestHash -cnotmatch '^[0-9a-f]{64}$') {
        throw 'The existing Pixels Setup service has an invalid package identity.'
    }
    Assert-StagedPackage $staged $previousManifestHash
    if ($previousManifestHash -cne $ExpectedManifestSha256) {
        $previousStage = Join-Path $install "setup_payload.previous.$([Guid]::NewGuid().ToString('N'))"
        if (-not [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($staged)).Equals(
                $install, [StringComparison]::OrdinalIgnoreCase) -or
            -not [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($previousStage)).Equals(
                $install, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Setup staging paths must stay directly inside the install root.'
        }
        if (Test-Path -LiteralPath $previousStage) { throw 'A previous setup staging path already exists.' }
        Stop-SetupService
        $previousStageMoved = $false
        try {
            Move-Item -LiteralPath $staged -Destination $previousStage
            $previousStageMoved = $true
            New-Item -ItemType Directory -Path $staged | Out-Null
            foreach ($relativePath in $actualPaths) {
                $destination = Join-Path $staged $relativePath
                New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
                Copy-Item -LiteralPath (Join-Path $package $relativePath) -Destination $destination
            }
            Assert-StagedPackage $staged $ExpectedManifestSha256
            $nextCommand = "`"$expectedExecutable`" setup-service `"$config`" `"$data`" `"$(Join-Path $install 'current')`" `"$staged`" $ExpectedManifestSha256"
            Invoke-Sc -Arguments @('config', 'Pixels.Setup', 'binPath=', $nextCommand.Replace('"', '\"'))
            Ensure-SetupFirewallRule $expectedExecutable
            Invoke-Sc -Arguments @('start', 'Pixels.Setup')
            Wait-SetupPage
        } catch {
            $upgradeFailure = $_
            try {
                Stop-SetupService
                if ($previousStageMoved) {
                    if (Test-Path -LiteralPath $staged) { Remove-Item -LiteralPath $staged -Recurse -Force }
                    Move-Item -LiteralPath $previousStage -Destination $staged
                }
                Invoke-Sc -Arguments @('config', 'Pixels.Setup', 'binPath=', $installedCommand.Replace('"', '\"'))
                Ensure-SetupFirewallRule $expectedExecutable
                Invoke-Sc -Arguments @('start', 'Pixels.Setup')
                Wait-SetupPage
            } catch {
                throw "Setup overwrite failed and rollback needs attention: $upgradeFailure; rollback: $_"
            }
            throw "Setup overwrite failed; the previous setup was restored: $upgradeFailure"
        }
        try {
            Remove-Item -LiteralPath $previousStage -Recurse -Force
        } catch {
            Write-Warning 'The previous setup payload was retained after a successful overwrite.'
        }
        Write-Output 'SETUP_READY http://127.0.0.1:4700/'
        return
    }
    if ($service.Status -ne 'Running') {
        Invoke-Sc -Arguments @('start', 'Pixels.Setup')
    }
    Ensure-SetupFirewallRule $expectedExecutable
    Wait-SetupPage
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
Ensure-SetupFirewallRule $executable
Wait-SetupPage
Write-Output 'SETUP_READY http://127.0.0.1:4700/'
