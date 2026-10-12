#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$BundleRoot,[switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$bundleDirectory = [IO.Path]::GetFullPath($BundleRoot)
$plan = Get-Content -LiteralPath (Join-Path $bundleDirectory 'update-plan.json') -Raw | ConvertFrom-Json
$installedDirectory = [IO.Path]::GetFullPath([string]$plan.installed_root)
if ($installedDirectory -ine 'C:\Program Files\Pixels Client' -or $null -ne $plan.service_name -or
    $plan.manifest_name -ne 'sha256sums.json' -or @($plan.removals).Count -ne 0) { throw 'Unsupported Panel update scope' }
function Resolve-ContainedFile([string]$Root,[string]$RelativePath) {
    $resolvedPath = [IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    if (-not $resolvedPath.StartsWith($Root.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Update path escapes directory' }
    $ancestorPath = $resolvedPath
    while ($ancestorPath -and $ancestorPath.StartsWith($Root,[StringComparison]::OrdinalIgnoreCase)) {
        if ((Test-Path -LiteralPath $ancestorPath) -and
            ((Get-Item -LiteralPath $ancestorPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Update traverses a reparse point' }
        $ancestorPath = [IO.Path]::GetDirectoryName($ancestorPath)
    }
    return $resolvedPath
}
function Assert-FileHash([string]$Path,[string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $Path).Hash -ine $Expected) { throw "Artifact identity differs: $Path" }
}
$manifestPath = Resolve-ContainedFile $installedDirectory 'sha256sums.json'
Assert-FileHash $manifestPath ([string]$plan.baseline_manifest_sha256)
$desiredPath = Resolve-ContainedFile $bundleDirectory 'desired-manifest.json'
Assert-FileHash $desiredPath ([string]$plan.desired_manifest_sha256)
if (@($plan.replacements).Count -ne 2) { throw 'Expected only Panel and its product metadata' }
foreach ($replacement in $plan.replacements) {
    if ($replacement.path -notin @('px_panel.exe','product-manifest.json')) { throw 'Unexpected changed runtime' }
    Assert-FileHash (Resolve-ContainedFile $installedDirectory $replacement.path) $replacement.before_sha256
    Assert-FileHash (Resolve-ContainedFile $bundleDirectory ('payload\' + $replacement.path)) $replacement.after_sha256
}
foreach ($runtime in @($plan.runtime_processes)) {
    if ($runtime.path -ne 'px_panel.exe') { throw 'Unexpected process shutdown scope' }
    $process = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$runtime.pid)
    if ($process -and ($process.ExecutablePath -ine (Join-Path $installedDirectory 'px_panel.exe') -or
        $process.CreationDate.ToUniversalTime().ToString('o') -ne $runtime.created_utc)) { throw 'Panel PID identity changed' }
}
if ($VerifyOnly) { [ordered]@{state='verified';changed_files=2}|ConvertTo-Json -Compress; return }
$backupDirectory = Join-Path $bundleDirectory ('before-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($backupDirectory)
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $backupDirectory 'sha256sums.json')
foreach ($replacement in $plan.replacements) {
    Copy-Item -LiteralPath (Join-Path $installedDirectory $replacement.path) -Destination (Join-Path $backupDirectory $replacement.path)
}
foreach ($runtime in @($plan.runtime_processes)) {
    $process = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$runtime.pid)
    if (-not $process) { continue }
    if ($process.ExecutablePath -ine (Join-Path $installedDirectory 'px_panel.exe') -or
        $process.CreationDate.ToUniversalTime().ToString('o') -ne $runtime.created_utc) { throw 'Panel identity changed before publication' }
    $processHandle = [Diagnostics.Process]::GetProcessById([int]$runtime.pid)
    try {
        $processHandle.Kill()
        if (-not $processHandle.WaitForExit(15000)) { throw 'Panel did not exit' }
    } finally { $processHandle.Dispose() }
}
foreach ($replacement in $plan.replacements) {
    $destination = Resolve-ContainedFile $installedDirectory $replacement.path
    Copy-Item -LiteralPath (Resolve-ContainedFile $bundleDirectory ('payload\' + $replacement.path)) -Destination $destination -Force
    Assert-FileHash $destination $replacement.after_sha256
}
Copy-Item -LiteralPath $desiredPath -Destination $manifestPath -Force
Assert-FileHash $manifestPath ([string]$plan.desired_manifest_sha256)
$inventory = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($artifact in $inventory.PSObject.Properties) {
    Assert-FileHash (Resolve-ContainedFile $installedDirectory $artifact.Name) ([string]$artifact.Value)
}
$product = Get-Content -LiteralPath (Join-Path $installedDirectory 'product-manifest.json') -Raw | ConvertFrom-Json
foreach ($artifact in $product.artifacts) {
    Assert-FileHash (Resolve-ContainedFile $installedDirectory $artifact.path) ([string]$artifact.sha256)
}
[ordered]@{state='published';backup_directory=$backupDirectory;files_verified=@($inventory.PSObject.Properties).Count;
    panel_sha256=(Get-FileHash -LiteralPath (Join-Path $installedDirectory 'px_panel.exe')).Hash;
    manifest_sha256=(Get-FileHash -LiteralPath $manifestPath).Hash}|ConvertTo-Json -Compress
