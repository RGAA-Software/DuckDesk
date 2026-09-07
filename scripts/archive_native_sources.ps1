param([string]$Batch = 'native_transport_simplification')

$ErrorActionPreference = 'Stop'
$archiveRepo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$archiveRoot = [IO.Path]::GetFullPath((Join-Path $archiveRepo "backup/$Batch"))
if (-not $archiveRoot.StartsWith((Join-Path $archiveRepo 'backup/'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Archive batch must remain beneath the repository backup directory.'
}
if (Test-Path -LiteralPath $archiveRoot) { throw "Archive already exists: $archiveRoot" }
$archiveRevision = (& git -C $archiveRepo rev-parse HEAD).Trim()
$archivePaths = @('src/px_deps/px_client_sdk', 'src/px_client_sdk', 'src/px_client', 'src/px_panel', 'src/px_android',
    'src/px_deps/CMakeLists.txt', 'src/CMakeLists.txt', 'CMakeLists.txt', 'scripts',
    'build_cpp_client.bat', 'build_cpp_sdk.bat', 'build_cpp_tests.bat', 'build_official.bat')
$archiveFiles = & git -C $archiveRepo ls-files --cached --others --exclude-standard -- $archivePaths
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate tracked source files.' }
$archiveManifest = foreach ($relative in $archiveFiles) {
    $source = Join-Path $archiveRepo $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
    $destination = Join-Path $archiveRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
    $originalHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ($originalHash -ne (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {
        throw "Archive hash mismatch: $relative"
    }
    [ordered]@{ path = $relative; sha256 = $originalHash; status = ((& git -C $archiveRepo status --porcelain -- $relative) -join "`n") }
}
$manifest = [ordered]@{ revision = $archiveRevision; reason = 'Native SDK extraction and single UDP/FEC + WebSocket transport'; files = @($archiveManifest) }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $archiveRoot 'manifest.json') -Encoding UTF8
Write-Host "Archived $($archiveManifest.Count) files with verified SHA-256: $archiveRoot"
