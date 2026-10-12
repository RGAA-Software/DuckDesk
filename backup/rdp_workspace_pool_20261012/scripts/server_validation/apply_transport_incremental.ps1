#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BundleRoot,
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$bundleDirectory = [IO.Path]::GetFullPath($BundleRoot)
$plan = Get-Content -LiteralPath (Join-Path $bundleDirectory 'update-plan.json') -Raw | ConvertFrom-Json
$installedDirectory = [IO.Path]::GetFullPath([string]$plan.installed_root)
if ($plan.service_name -notin @('Pixels.Console','px_service')) { throw 'Unsupported service in transport update' }

function Resolve-ContainedFile([string]$Root, [string]$RelativePath) {
    $resolvedPath = [IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    if (-not $resolvedPath.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Update path leaves its declared directory'
    }
    $ancestorPath = $resolvedPath
    while ($ancestorPath -and $ancestorPath.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $ancestorPath) {
            if (((Get-Item -LiteralPath $ancestorPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Update path traverses a reparse point: $ancestorPath"
            }
        }
        $ancestorPath = [IO.Path]::GetDirectoryName($ancestorPath)
    }
    return $resolvedPath
}

function Assert-FileHash([string]$Path, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Expected) {
        throw "Artifact identity differs: $Path"
    }
}

$manifestPath = Resolve-ContainedFile $installedDirectory ([string]$plan.manifest_name)
Assert-FileHash $manifestPath ([string]$plan.baseline_manifest_sha256)
foreach ($replacement in $plan.replacements) {
    $destinationPath = Resolve-ContainedFile $installedDirectory ([string]$replacement.path)
    $payloadPath = Resolve-ContainedFile $bundleDirectory ('payload\' + [string]$replacement.path)
    Assert-FileHash $payloadPath ([string]$replacement.after_sha256)
    if ($replacement.before_sha256) { Assert-FileHash $destinationPath ([string]$replacement.before_sha256) }
    elseif (Test-Path -LiteralPath $destinationPath) { throw "Unexpected existing new artifact: $destinationPath" }
}
foreach ($retired in $plan.removals) {
    Assert-FileHash (Resolve-ContainedFile $installedDirectory ([string]$retired.path)) ([string]$retired.before_sha256)
}
$desiredManifestPath = Resolve-ContainedFile $bundleDirectory 'desired-manifest.json'
Assert-FileHash $desiredManifestPath ([string]$plan.desired_manifest_sha256)
$desiredManifest = Get-Content -LiteralPath $desiredManifestPath -Raw | ConvertFrom-Json
$desiredFiles = if ($plan.manifest_name -eq 'sha256.json') { $desiredManifest.files } else { $desiredManifest }
$windowsService = Get-CimInstance Win32_Service -Filter ("Name='" + [string]$plan.service_name + "'")
if (-not $windowsService) { throw 'Expected installed service missing' }
$expectedExecutable = Resolve-ContainedFile $installedDirectory ([string]$plan.runtime_executable)
if ($windowsService.ProcessId -ne 0) {
    $serviceProcess = Get-CimInstance Win32_Process -Filter ("ProcessId=" + $windowsService.ProcessId)
    if ([IO.Path]::GetFullPath($serviceProcess.ExecutablePath) -ine $expectedExecutable) { throw 'Installed service executable differs' }
}
foreach ($runtime in @($plan.runtime_processes)) {
    if ($null -eq $runtime) { continue }
    $expectedPath = Resolve-ContainedFile $installedDirectory ([string]$runtime.path)
    $runtimeProcess = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$runtime.pid)
    if ($null -ne $runtimeProcess -and
        ([IO.Path]::GetFullPath($runtimeProcess.ExecutablePath) -ine $expectedPath -or
         $runtimeProcess.CreationDate.ToUniversalTime().ToString('o') -ne [string]$runtime.created_utc)) {
        throw 'Runtime PID was reused or its identity changed'
    }
}
if ($VerifyOnly) {
    [pscustomobject]@{state='verified';service=$plan.service_name;changed_files=@($plan.replacements).Count;baseline_manifest_sha256=$plan.baseline_manifest_sha256} | ConvertTo-Json -Compress
    return
}

$backupDirectory = Join-Path $bundleDirectory ('before-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($backupDirectory) | Out-Null
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $backupDirectory $plan.manifest_name)
foreach ($artifact in @($plan.replacements) + @($plan.removals)) {
    $existingPath = Resolve-ContainedFile $installedDirectory ([string]$artifact.path)
    if (Test-Path -LiteralPath $existingPath) {
        $backupPath = Resolve-ContainedFile $backupDirectory ([string]$artifact.path)
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backupPath)) | Out-Null
        Copy-Item -LiteralPath $existingPath -Destination $backupPath
    }
}
Stop-Service -Name $plan.service_name -ErrorAction Stop
(Get-Service -Name $plan.service_name).WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds(30))
foreach ($runtime in @($plan.runtime_processes)) {
    if ($null -eq $runtime) { continue }
    $runtimeProcess = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$runtime.pid)
    if ($null -eq $runtimeProcess) { continue }
    if ([IO.Path]::GetFullPath($runtimeProcess.ExecutablePath) -ine (Resolve-ContainedFile $installedDirectory ([string]$runtime.path)) -or
        $runtimeProcess.CreationDate.ToUniversalTime().ToString('o') -ne [string]$runtime.created_utc) {
        throw 'Runtime changed before publication'
    }
    Stop-Process -Id ([int]$runtime.pid) -ErrorAction Stop
    $shutdownDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $remainingProcess = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$runtime.pid)
        if ($null -eq $remainingProcess) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $shutdownDeadline)
    if ($null -ne $remainingProcess) { throw 'Explicit runtime did not stop' }
}
foreach ($replacement in $plan.replacements) {
    $destinationPath = Resolve-ContainedFile $installedDirectory ([string]$replacement.path)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destinationPath)) | Out-Null
    Copy-Item -LiteralPath (Resolve-ContainedFile $bundleDirectory ('payload\' + [string]$replacement.path)) -Destination $destinationPath -Force
    Assert-FileHash $destinationPath ([string]$replacement.after_sha256)
}
foreach ($retired in $plan.removals) {
    Remove-Item -LiteralPath (Resolve-ContainedFile $installedDirectory ([string]$retired.path))
}
Copy-Item -LiteralPath $desiredManifestPath -Destination $manifestPath -Force
foreach ($property in $desiredFiles.PSObject.Properties) {
    Assert-FileHash (Resolve-ContainedFile $installedDirectory $property.Name) ([string]$property.Value)
}
if ($plan.manifest_name -eq 'sha256sums.json') {
    $productManifest = Get-Content -LiteralPath (Resolve-ContainedFile $installedDirectory 'product-manifest.json') -Raw | ConvertFrom-Json
    foreach ($artifact in $productManifest.artifacts) {
        Assert-FileHash (Resolve-ContainedFile $installedDirectory ([string]$artifact.path)) ([string]$artifact.sha256)
    }
}
Start-Service -Name $plan.service_name -ErrorAction Stop
(Get-Service -Name $plan.service_name).WaitForStatus([ServiceProcess.ServiceControllerStatus]::Running, [TimeSpan]::FromSeconds(30))
[pscustomobject]@{state='published';service=$plan.service_name;runtime_sha256=(Get-FileHash -LiteralPath $expectedExecutable -Algorithm SHA256).Hash;
    manifest_sha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash;verified_artifacts=@($desiredFiles.PSObject.Properties).Count + 1;
    backup_directory=$backupDirectory;service_state=(Get-Service -Name $plan.service_name).Status.ToString()} | ConvertTo-Json -Compress
