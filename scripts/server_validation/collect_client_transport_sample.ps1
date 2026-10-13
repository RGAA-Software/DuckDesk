param(
    [Parameter(Mandatory)] [string]$ClientReportPath,
    [string]$ClientRoot = 'C:\Program Files\Pixels Client'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$clientReports = @(Get-Content -LiteralPath $ClientReportPath -Raw | ConvertFrom-Json)
if ($clientReports.Count -ne 1 -or $clientReports[0].state -ne 'closed') { throw 'Expected one completed owned Client sample' }
$clientReport = $clientReports[0]
$sampleStart = ([DateTimeOffset]::Parse($clientReport.started_at_utc)).LocalDateTime
$sampleEnd = ([DateTimeOffset]::Parse($clientReport.closed_at_utc)).LocalDateTime
$archivePath = Join-Path (Split-Path -Parent $ClientReportPath) 'client.log.gz'
if (Test-Path -LiteralPath $archivePath) { throw 'Preserve the existing sample archive' }
$sourcePath = Join-Path $ClientRoot 'px_logs\px_client.log'
$sourceStream = [IO.File]::Open($sourcePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
$reader = [IO.StreamReader]::new($sourceStream, [Text.Encoding]::UTF8)
$destinationStream = [IO.File]::Create($archivePath)
$gzip = [IO.Compression.GZipStream]::new($destinationStream, [IO.Compression.CompressionMode]::Compress, $true)
$writer = [IO.StreamWriter]::new($gzip, [Text.UTF8Encoding]::new($false))
$evidenceLines = [Collections.Generic.List[string]]::new()
$writtenLines = 0
try {
    while (-not $reader.EndOfStream) {
        $logLine = $reader.ReadLine()
        if ($logLine -notmatch '^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]') { continue }
        $observedAt = [DateTime]::ParseExact($matches[1], 'yyyy-MM-dd HH:mm:ss.fff', [Globalization.CultureInfo]::InvariantCulture)
        if ($observedAt -lt $sampleStart -or $observedAt -gt $sampleEnd) { continue }
        $writer.WriteLine($logLine)
        ++$writtenLines
        if ($logLine -match 'event=video.present phase=(window|gap)|event=client.audio_output|event=iroh.connect|event=session.hello|event=iroh.reliable_send|event=rdp.frame.progress') {
            $evidenceLines.Add($logLine)
        }
    }
} finally {
    $writer.Dispose()
    $gzip.Dispose()
    $destinationStream.Dispose()
    $reader.Dispose()
}
[ordered]@{
    client_report = $clientReport
    archive_path = $archivePath
    archive_sha256 = (Get-FileHash -LiteralPath $archivePath).Hash
    archive_bytes = (Get-Item -LiteralPath $archivePath).Length
    written_lines = $writtenLines
    evidence_lines = $evidenceLines.ToArray()
    remaining_clients = @(Get-CimInstance Win32_Process | Where-Object Name -eq 'px_client.exe' | Select-Object ProcessId, ExecutablePath)
} | ConvertTo-Json -Depth 5
