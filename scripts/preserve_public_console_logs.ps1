#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$ServerRoot = 'D:\PixelsServer'
)

$ErrorActionPreference = 'Stop'
$logsDirectory = [IO.Path]::GetFullPath((Join-Path $ServerRoot 'logs'))
$historyDirectory = Join-Path $logsDirectory 'history'
if (-not (Test-Path -LiteralPath $logsDirectory -PathType Container)) {
    throw "Console log directory is missing: $logsDirectory"
}

$timestamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ')
foreach ($logName in @('console.stdout.log', 'console.stderr.log')) {
    $logPath = Join-Path $logsDirectory $logName
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf) -or (Get-Item -LiteralPath $logPath).Length -eq 0) {
        continue
    }
    [void](New-Item -ItemType Directory -Path $historyDirectory -Force)
    $archivePath = Join-Path $historyDirectory "$timestamp.$logName"
    try {
        $sourceStream = [IO.File]::Open($logPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $hasher = [Security.Cryptography.SHA256]::Create()
            try {
                $expectedHash = [BitConverter]::ToString($hasher.ComputeHash($sourceStream)).Replace('-', '')
            } finally {
                $hasher.Dispose()
            }
            $sourceStream.Position = 0
            $archiveStream = [IO.File]::Open($archivePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try {
                $sourceStream.CopyTo($archiveStream)
            } finally {
                $archiveStream.Dispose()
            }
        } finally {
            $sourceStream.Dispose()
        }
        if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $expectedHash) {
            throw "Console log archive hash mismatch: $logName"
        }
    } catch {
        if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
            Remove-Item -LiteralPath $archivePath -Force
        }
        Write-Warning "Console log could not be preserved before restart: $logName"
    }
}
