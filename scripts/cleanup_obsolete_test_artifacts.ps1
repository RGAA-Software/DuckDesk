# One-time cleanup of the obsolete local test artifacts reviewed on 2026-09-08.
# Preview: pwsh -NoProfile -File scripts/cleanup_obsolete_test_artifacts.ps1
# Delete:  pwsh -NoProfile -File scripts/cleanup_obsolete_test_artifacts.ps1 -Execute
# Deletion is permanent (not the Recycle Bin). No services/processes are stopped.
[CmdletBinding()]
param([switch]$Execute)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$cleanupRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$cleanupPrefix = $cleanupRoot + [IO.Path]::DirectorySeparatorChar
if (-not (Test-Path -LiteralPath (Join-Path $cleanupRoot 'AGENTS.md') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $cleanupRoot 'CMakeLists.txt') -PathType Leaf)) {
    throw 'Run this script from its original repository scripts directory.'
}
$gitRoot = & git -C $cleanupRoot rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($gitRoot) -ne $cleanupRoot) {
    throw 'Cannot verify the Git repository root.'
}

# Explicit directory allowlist. Current build_official, build_sdk_*, Android builds,
# normal Rust target directories, output, source trees and backup are NOT included.
$cacheDirectories = @(
    'target_cms_final_20260820',
    'target_event_20260821',
    'target_service_final_20260820',
    'rust_client/target_codex',
    'rust_client/target_user_acl_20260820',
    'rust_server/target_cms_tests_20260820',
    'rust_server/target_codex'
)
$otherDirectories = @(
    'build_probe',
    'test-results/recording-forced-termination',
    'test-results/ws-ipc-dumps'
)
$rootScreenshots = @(
    'android_current.png',
    'android_rtc_after_redis.png',
    'android_rtc_retest.png',
    'android_rtc_retest2.png',
    'android_rtc_retest3.png',
    'android_toolbar.png',
    'android_toolbar2.png',
    'android_voice1.png',
    'android_voice2.png'
)

function Assert-SafePath([string]$RelativePath) {
    if ([IO.Path]::IsPathRooted($RelativePath) -or $RelativePath -match '(^|[\\/])\.\.([\\/]|$)') {
        throw "Invalid relative path: $RelativePath"
    }
    $absolutePath = [IO.Path]::GetFullPath((Join-Path $cleanupRoot $RelativePath))
    if (-not $absolutePath.StartsWith($cleanupPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Target is outside the repository: $RelativePath"
    }
    # Check every ancestor as well as the target, before any recursive traversal.
    $ancestor = $absolutePath
    while ($ancestor.Length -ge $cleanupRoot.Length) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing junction/symlink: $ancestor"
            }
        }
        if ($ancestor -eq $cleanupRoot) { break }
        $ancestor = Split-Path -Parent $ancestor
    }
    return $absolutePath
}

function Get-TargetInventory([string]$AbsolutePath) {
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($AbsolutePath)
    $fileCount = 0L
    $byteCount = 0L
    $latestWrite = [datetime]::MinValue
    while ($pending.Count -gt 0) {
        $entry = Get-Item -LiteralPath $pending.Pop() -Force
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing junction/symlink: $($entry.FullName)"
        }
        if ($entry.PSIsContainer) {
            foreach ($child in @(Get-ChildItem -LiteralPath $entry.FullName -Force)) {
                $pending.Push($child.FullName)
            }
        } else {
            $fileCount++
            $byteCount += $entry.Length
            if ($entry.LastWriteTime -gt $latestWrite) { $latestWrite = $entry.LastWriteTime }
        }
    }
    return [pscustomobject]@{ Files = $fileCount; Bytes = $byteCount; LatestWrite = $latestWrite }
}

function Assert-Untracked([string]$RelativePath) {
    $tracked = @(& git -C $cleanupRoot ls-files -- ":(literal)$RelativePath")
    if ($LASTEXITCODE -ne 0 -or $tracked.Count -ne 0) {
        throw "Refusing tracked files or failed Git check: $RelativePath"
    }
}

function Assert-NotInUse([object[]]$Targets) {
    $processes = @(Get-CimInstance Win32_Process)
    foreach ($process in $processes) {
        if ($process.ProcessId -eq $PID) { continue }
        # Conservatively wait for builds to finish, even when their working directory
        # cannot be obtained. Do not terminate another project or a local service.
        if ($process.Name -match '^(cargo|rustc|ninja|cmake|ctest|cl|link)\.exe$') {
            throw "Build/test process is active: $($process.Name) PID $($process.ProcessId). Retry after it finishes."
        }
        $commandLine = ([string]$process.CommandLine).Replace('/', '\')
        $executable = [string]$process.ExecutablePath
        foreach ($target in $Targets) {
            if ($executable.Equals($target.Path, [StringComparison]::OrdinalIgnoreCase) -or
                $executable.StartsWith($target.Path + '\', [StringComparison]::OrdinalIgnoreCase) -or
                $commandLine.IndexOf($target.Path, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "Target is referenced by PID $($process.ProcessId): $($target.Relative)"
            }
        }
    }
}

$relativeTargets = @($cacheDirectories + $otherDirectories + $rootScreenshots)
$resultsPath = Assert-SafePath 'test-results'
if (Test-Path -LiteralPath $resultsPath -PathType Container) {
    # Top-level generated captures only; never recurse into the user source backup.
    # Preserve logs, reports, JSON, source backups, credentials and the old server EXE.
    $captureExtensions = @('.apk', '.png', '.mp4', '.wav', '.h264', '.download', '.xml')
    $relativeTargets += @(Get-ChildItem -LiteralPath $resultsPath -File -Force |
        Where-Object { $_.Extension -in $captureExtensions } |
        ForEach-Object { 'test-results/' + $_.Name })
}

$plan = @(
    foreach ($relativeTarget in @($relativeTargets | Sort-Object -Unique)) {
        $absoluteTarget = Assert-SafePath $relativeTarget
        if (-not (Test-Path -LiteralPath $absoluteTarget)) { continue }
        Assert-Untracked $relativeTarget
        if ($relativeTarget -in $cacheDirectories -and
            -not (Test-Path -LiteralPath (Join-Path $absoluteTarget 'CACHEDIR.TAG') -PathType Leaf)) {
            throw "Expected Rust cache marker is missing: $relativeTarget"
        }
        $inventory = Get-TargetInventory $absoluteTarget
        # This is a dated cleanup, not an unlimited future cleanup policy.
        if ($inventory.LatestWrite -ge [datetime]'2026-09-09' -or
            $inventory.LatestWrite -gt (Get-Date).AddMinutes(-15)) {
            Write-Warning "Preserving newer/recently active artifacts: $relativeTarget"
            continue
        }
        [pscustomobject]@{
            Relative = $relativeTarget
            Path = $absoluteTarget
            IsDirectory = (Get-Item -LiteralPath $absoluteTarget).PSIsContainer
            Files = $inventory.Files
            Bytes = $inventory.Bytes
            LatestWrite = $inventory.LatestWrite
        }
    }
)

if ($plan.Count -eq 0) {
    Write-Host 'No obsolete artifacts remain in the allowlist.'
    return
}
$plan | Select-Object Relative, Files, @{Name = 'MiB'; Expression = { [math]::Round($_.Bytes / 1MB, 2) }} |
    Format-Table -AutoSize
$totalBytes = ($plan | Measure-Object Bytes -Sum).Sum
Write-Host ('Total: {0} targets, {1:N2} GiB. Preserved: source, backups, current builds and test logs.' -f
    $plan.Count, ($totalBytes / 1GB))
if (-not $Execute) {
    Write-Host 'PREVIEW ONLY: nothing deleted. Add -Execute to permanently delete these artifacts.'
    return
}

Assert-NotInUse $plan
# Validate the entire plan again before deleting the first item.
foreach ($target in $plan) {
    $validatedPath = Assert-SafePath $target.Relative
    Assert-Untracked $target.Relative
    $current = Get-TargetInventory $validatedPath
    if ($current.Files -ne $target.Files -or $current.Bytes -ne $target.Bytes -or $current.LatestWrite -ne $target.LatestWrite) {
        throw "Target changed during preview; nothing has been deleted: $($target.Relative)"
    }
}

$deletedBytes = 0L
$deletedFiles = 0L
try {
    foreach ($target in $plan) {
        Write-Host "Deleting: $($target.Relative)"
        if ($target.IsDirectory) {
            Remove-Item -LiteralPath $target.Path -Recurse -Force -ErrorAction Stop
        } else {
            Remove-Item -LiteralPath $target.Path -Force -ErrorAction Stop
        }
        if (Test-Path -LiteralPath $target.Path) { throw "Target still exists: $($target.Relative)" }
        $deletedBytes += $target.Bytes
        $deletedFiles += $target.Files
    }
} catch {
    Write-Warning ('Cleanup stopped. Fully removed targets accounted for {0} files, {1:N2} GiB; the failing target may be partially removed.' -f
        $deletedFiles, ($deletedBytes / 1GB))
    throw
}
Write-Host ('DONE: permanently deleted {0} files, {1:N2} GiB. Caches can be rebuilt; deleted captures cannot be restored by this script.' -f
    $deletedFiles, ($deletedBytes / 1GB))
