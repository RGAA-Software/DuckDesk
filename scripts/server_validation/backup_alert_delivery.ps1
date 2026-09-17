#requires -Version 7.0
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dockerDirectory = 'C:\Program Files\Docker\Docker\resources\bin'
$dockerExecutable = Join-Path $dockerDirectory 'docker.exe'
if (-not (Test-Path -LiteralPath $dockerExecutable)) {
    throw 'Docker Desktop command line is unavailable.'
}
$env:Path = "$dockerDirectory;$env:Path"

$runIdentity = [guid]::NewGuid().ToString('N')
$resourceSuffix = $runIdentity.Substring(0, 12)
$networkName = "pixels-backup-alert-$resourceSuffix"
$nodeExporterName = "pixels-backup-node-$resourceSuffix"
$alertmanagerName = "pixels-backup-alertmanager-$resourceSuffix"
$prometheusName = "pixels-backup-prometheus-$resourceSuffix"
$resultDirectory = Join-Path $repositoryRoot "test-results/backup-alert-$resourceSuffix"
$metricsDirectory = Join-Path $resultDirectory 'metrics'
$configurationDirectory = Join-Path $resultDirectory 'configuration'
$deliveryResultPath = Join-Path $resultDirectory 'delivery.json'
$receiverStdoutPath = Join-Path $resultDirectory 'receiver.stdout.log'
$receiverStderrPath = Join-Path $resultDirectory 'receiver.stderr.log'
New-Item -ItemType Directory -Path $metricsDirectory, $configurationDirectory -Force | Out-Null

function Invoke-Docker([string[]]$Arguments) {
    $commandOutput = & $dockerExecutable @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Docker command failed: $($Arguments -join ' ')`n$commandOutput"
    }
    return ($commandOutput -join "`n").Trim()
}

function Get-AvailablePort {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally {
        $listener.Stop()
    }
}

function Convert-ToContainerPath([string]$WindowsPath) {
    return $WindowsPath.Replace('\', '/')
}

$receiverPort = Get-AvailablePort
$deploymentId = [guid]::NewGuid().ToString()
$metricsText = @"
# HELP pixels_backup_status_timestamp_seconds Unix timestamp of the latest backup daemon status publication.
# TYPE pixels_backup_status_timestamp_seconds gauge
pixels_backup_status_timestamp_seconds{deployment_id="$deploymentId"} $([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
# HELP pixels_backup_poll_interval_seconds Configured backup daemon status polling interval.
# TYPE pixels_backup_poll_interval_seconds gauge
pixels_backup_poll_interval_seconds{deployment_id="$deploymentId"} 30
# HELP pixels_backup_alert_consecutive_failures Whether the consecutive-failure alert condition is active.
# TYPE pixels_backup_alert_consecutive_failures gauge
pixels_backup_alert_consecutive_failures{deployment_id="$deploymentId"} 1
# HELP pixels_backup_alert_overdue Whether the verified-backup overdue alert condition is active.
# TYPE pixels_backup_alert_overdue gauge
pixels_backup_alert_overdue{deployment_id="$deploymentId"} 0
# HELP pixels_backup_offsite_configured Whether an offsite repository is required by this deployment.
# TYPE pixels_backup_offsite_configured gauge
pixels_backup_offsite_configured{deployment_id="$deploymentId"} 0
# HELP pixels_backup_offsite_repository_healthy Whether the configured offsite repository can be verified.
# TYPE pixels_backup_offsite_repository_healthy gauge
pixels_backup_offsite_repository_healthy{deployment_id="$deploymentId"} 0
"@
[IO.File]::WriteAllText((Join-Path $metricsDirectory 'metrics.prom'), "$metricsText`n")

$prometheusConfiguration = @"
global:
  scrape_interval: 1s
  evaluation_interval: 1s
rule_files:
  - /etc/prometheus/pixels-backup.rules.yml
alerting:
  alertmanagers:
    - static_configs:
        - targets: [alertmanager:9093]
scrape_configs:
  - job_name: pixels-backup-validation
    static_configs:
      - targets: [node-exporter:9100]
"@
[IO.File]::WriteAllText((Join-Path $configurationDirectory 'prometheus.yml'), $prometheusConfiguration)

$alertmanagerConfiguration = @"
route:
  receiver: validation-receiver
  group_wait: 0s
  group_interval: 1s
  repeat_interval: 1h
receivers:
  - name: validation-receiver
    webhook_configs:
      - url: http://host.docker.internal:$receiverPort/alerts
        send_resolved: false
"@
[IO.File]::WriteAllText((Join-Path $configurationDirectory 'alertmanager.yml'), $alertmanagerConfiguration)

$receiverProcess = $null
try {
    Invoke-Docker @('pull', 'prom/node-exporter:v1.9.1') | Out-Null
    Invoke-Docker @('pull', 'prom/alertmanager:v0.28.1') | Out-Null
    Invoke-Docker @('pull', 'prom/prometheus:v3.5.0') | Out-Null
    Invoke-Docker @('network', 'create', $networkName) | Out-Null

    $receiverStart = [Diagnostics.ProcessStartInfo]::new()
    $receiverStart.FileName = (Get-Command node).Source
    $receiverStart.UseShellExecute = $false
    $receiverStart.CreateNoWindow = $true
    $receiverStart.RedirectStandardOutput = $true
    $receiverStart.RedirectStandardError = $true
    $receiverStart.ArgumentList.Add((Join-Path $PSScriptRoot 'backup_alert_receiver.cjs'))
    $receiverStart.ArgumentList.Add("$receiverPort")
    $receiverStart.ArgumentList.Add($deliveryResultPath)
    $receiverProcess = [Diagnostics.Process]::Start($receiverStart)
    Start-Sleep -Milliseconds 500
    if ($receiverProcess.HasExited) {
        throw 'Alert receiver failed before the monitoring stack started.'
    }

    $metricsMount = "$(Convert-ToContainerPath $metricsDirectory):/textfile:ro"
    Invoke-Docker @(
        'run', '--detach', '--name', $nodeExporterName, '--network', $networkName,
        '--network-alias', 'node-exporter', '--volume', $metricsMount,
        'prom/node-exporter:v1.9.1', '--collector.textfile.directory=/textfile'
    ) | Out-Null
    $alertmanagerMount = "$(Convert-ToContainerPath (Join-Path $configurationDirectory 'alertmanager.yml')):/etc/alertmanager/alertmanager.yml:ro"
    Invoke-Docker @(
        'run', '--detach', '--name', $alertmanagerName, '--network', $networkName,
        '--network-alias', 'alertmanager', '--volume', $alertmanagerMount,
        'prom/alertmanager:v0.28.1', '--config.file=/etc/alertmanager/alertmanager.yml'
    ) | Out-Null
    $prometheusMount = "$(Convert-ToContainerPath (Join-Path $configurationDirectory 'prometheus.yml')):/etc/prometheus/prometheus.yml:ro"
    $rulesMount = "$(Convert-ToContainerPath (Join-Path $repositoryRoot 'deploy/prometheus/pixels-backup.rules.yml')):/etc/prometheus/pixels-backup.rules.yml:ro"
    Invoke-Docker @(
        'run', '--detach', '--name', $prometheusName, '--network', $networkName,
        '--volume', $prometheusMount, '--volume', $rulesMount,
        'prom/prometheus:v3.5.0', '--config.file=/etc/prometheus/prometheus.yml'
    ) | Out-Null

    if (-not $receiverProcess.WaitForExit(150000)) {
        throw 'Prometheus did not deliver the firing backup alert through Alertmanager within 150 seconds.'
    }
    [IO.File]::WriteAllText($receiverStdoutPath, $receiverProcess.StandardOutput.ReadToEnd())
    [IO.File]::WriteAllText($receiverStderrPath, $receiverProcess.StandardError.ReadToEnd())
    if ($receiverProcess.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $deliveryResultPath)) {
        throw "Alert receiver failed with exit code $($receiverProcess.ExitCode)."
    }
    $deliveryResult = Get-Content -LiteralPath $deliveryResultPath -Raw | ConvertFrom-Json
    if ($deliveryResult.alertName -ne 'PixelsBackupRepeatedFailures' -or
        $deliveryResult.deploymentId -ne $deploymentId -or
        $deliveryResult.status -ne 'firing') {
        throw 'Delivered alert identity or status did not match the configured deployment.'
    }
    Write-Host "PASS backup-alert-delivery deployment=$deploymentId result=$deliveryResultPath"
} finally {
    if ($receiverProcess -and -not $receiverProcess.HasExited) {
        $receiverProcess.Kill($true)
        $receiverProcess.WaitForExit()
    }
    if ($receiverProcess) {
        if (-not (Test-Path -LiteralPath $receiverStdoutPath)) {
            [IO.File]::WriteAllText($receiverStdoutPath, $receiverProcess.StandardOutput.ReadToEnd())
        }
        if (-not (Test-Path -LiteralPath $receiverStderrPath)) {
            [IO.File]::WriteAllText($receiverStderrPath, $receiverProcess.StandardError.ReadToEnd())
        }
    }
    foreach ($containerName in @($prometheusName, $alertmanagerName, $nodeExporterName)) {
        $containerLogPath = Join-Path $resultDirectory "$containerName.log"
        $containerLogs = & $dockerExecutable logs $containerName 2>&1
        [IO.File]::WriteAllText($containerLogPath, ($containerLogs -join "`n"))
        & $dockerExecutable rm --force $containerName 2>$null | Out-Null
    }
    & $dockerExecutable network rm $networkName 2>$null | Out-Null
}
