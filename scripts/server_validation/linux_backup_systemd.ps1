#requires -Version 7.0
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$deploymentId = [guid]::NewGuid().ToString()
$serviceName = "pixels-backup@$deploymentId.service"
$temporaryDirectory = Join-Path $repo ".cache/systemd-$deploymentId"
$configPath = Join-Path $temporaryDirectory 'config.json'
$linuxRepo = '/mnt/d/GoCloud/GammaRayPremium'
$linuxBinary = "$linuxRepo/rust_server/target/debug/px_backup"
$linuxConfig = "$linuxRepo/.cache/systemd-$deploymentId/config.json"
$linuxInstaller = "$linuxRepo/scripts/server_backup/install_linux_service.sh"
$linuxUninstaller = "$linuxRepo/scripts/server_backup/uninstall_linux_service.sh"
$dataRoot = "/var/lib/pixels/$deploymentId/backup"
$configurationRoot = "/etc/pixels/$deploymentId"
$validationToolRoot = "/opt/pixels/systemd-validation-$deploymentId"
$startedAt = [DateTime]::UtcNow

function Invoke-Wsl([string]$Command, [switch]$Root) {
    $arguments = @()
    if ($Root) { $arguments += @('-u', 'root') }
    $arguments += @(
        '-e', 'env', '-i',
        'HOME=/home/chessplayer',
        'PATH=/home/chessplayer/.cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin',
        '/bin/bash', '--noprofile', '--norc', '-lc', $Command
    )
    $output = & wsl @arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "WSL command failed: $Command`n$($output -join "`n")" }
    return ($output -join "`n")
}

try {
    $pidOne = Invoke-Wsl 'ps -p 1 -o comm='
    if ($pidOne.Trim() -ne 'systemd') { throw "WSL PID 1 is not systemd: $pidOne" }
    Invoke-Wsl "cd '$linuxRepo/rust_server' && cargo build -p px_backup --offline --locked" | Out-Null
    Invoke-Wsl "install -d -o root -g root -m 0755 '$validationToolRoot'; install -o root -g root -m 0755 /usr/bin/true '$validationToolRoot/pg_dump'; install -o root -g root -m 0755 /usr/bin/true '$validationToolRoot/pg_restore'" -Root | Out-Null
    $toolSha256 = (Invoke-Wsl "sha256sum '$validationToolRoot/pg_dump' | cut -d' ' -f1").Trim()
    if ($toolSha256 -notmatch '^[0-9a-f]{64}$') { throw 'Could not hash Linux validation tool' }

    New-Item -ItemType Directory -Path $temporaryDirectory -Force | Out-Null
    $config = [ordered]@{
        schema_version = 2
        deployment_id = $deploymentId
        repository_root = "$dataRoot/repository"
        offsite_repository_root = $null
        scheduler_root = "$dataRoot/scheduler"
        status_root = "$dataRoot/status"
        pg_dump_path = "$validationToolRoot/pg_dump"
        pg_dump_sha256 = $toolSha256
        pg_restore_path = "$validationToolRoot/pg_restore"
        pg_restore_sha256 = $toolSha256
        command_timeout_seconds = 60
        poll_interval_seconds = 1
        schedule = [ordered]@{
            deployment_id = $deploymentId
            anchor_unix = 4102444800
            period_seconds = 3600
        }
        retention = [ordered]@{
            hourly = 24
            daily = 7
            weekly = 4
            monthly = 6
            pre_upgrade = 5
            manual_days = 30
        }
        offsite_retention = $null
        plan = [ordered]@{
            deployment_id = $deploymentId
            kind = 'independent'
            write_barrier_proof_file = $null
            retention = @('hourly')
            previous_recovery_set_id = $null
            targets = @(
                [ordered]@{
                    state = 'required'
                    database = [ordered]@{
                        service = 'console'
                        host = '127.0.0.1'
                        port = 5432
                        database = 'pixels_console'
                        username = 'pixels_backup'
                        password_file = "/etc/pixels/$deploymentId/backup/console.pgpass"
                        schema_version = 1
                    }
                },
                [ordered]@{ state = 'not_applicable'; service = 'auth'; reason = 'not installed in validation deployment' },
                [ordered]@{ state = 'not_applicable'; service = 'desk'; reason = 'not installed in validation deployment' }
            )
        }
    }
    $config | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $configPath -Encoding utf8NoBOM

    Invoke-Wsl "/bin/sh '$linuxInstaller' '$deploymentId' '$linuxBinary' '$linuxConfig'" -Root | Out-Null
    $activeState = Invoke-Wsl "systemctl is-active '$serviceName'" -Root
    if ($activeState.Trim() -ne 'active') { throw "Service did not become active: $activeState" }
    $statusPublished = $false
    for ($attempt = 1; $attempt -le 30; $attempt++) {
        & wsl -u root -e test -f "$dataRoot/status/status.json"
        if ($LASTEXITCODE -eq 0) { $statusPublished = $true; break }
        Start-Sleep -Milliseconds 200
    }
    if (-not $statusPublished) {
        $journal = Invoke-Wsl "journalctl -u '$serviceName' --no-pager -n 50" -Root
        throw "Service did not publish status:`n$journal"
    }
    $status = Invoke-Wsl "cat '$dataRoot/status/status.json'" -Root | ConvertFrom-Json
    if ($status.schema_version -ne 2 -or $status.deployment_id -ne $deploymentId) {
        throw 'systemd service published an invalid status document'
    }
    $repositoryLockBefore = Invoke-Wsl "sha256sum '$dataRoot/repository/repository.lock' | cut -d' ' -f1" -Root

    Invoke-Wsl "systemctl restart '$serviceName'" -Root | Out-Null
    if ((Invoke-Wsl "systemctl is-active '$serviceName'" -Root).Trim() -ne 'active') {
        throw 'systemd restart did not return the service to active state'
    }
    $repositoryLockAfter = Invoke-Wsl "sha256sum '$dataRoot/repository/repository.lock' | cut -d' ' -f1" -Root
    if ($repositoryLockAfter.Trim() -ne $repositoryLockBefore.Trim()) {
        throw 'repository lock identity changed across systemd restart'
    }

    Invoke-Wsl "systemctl stop '$serviceName'" -Root | Out-Null
    $stopResult = Invoke-Wsl "systemctl show '$serviceName' --property=ActiveState --property=Result --property=ExecMainStatus --value" -Root
    if ($stopResult -notmatch 'inactive' -or $stopResult -notmatch '(?m)^success$' -or $stopResult -notmatch '(?m)^0$') {
        throw "SIGTERM stop was not clean:`n$stopResult"
    }
    Invoke-Wsl "systemctl start '$serviceName' && systemctl is-active --quiet '$serviceName'" -Root | Out-Null
    Invoke-Wsl "/bin/sh '$linuxUninstaller' '$deploymentId'" -Root | Out-Null
    if ((Invoke-Wsl "systemctl is-enabled '$serviceName' 2>/dev/null || true" -Root).Trim() -eq 'enabled') {
        throw 'uninstall left the deployment instance enabled'
    }
    Invoke-Wsl "test -f '$configurationRoot/backup/config.json' && test -f '$dataRoot/repository/repository.lock'" -Root | Out-Null

    $reportDirectory = Join-Path $repo "test-results/server_validation/systemd-$deploymentId"
    New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
    [ordered]@{
        status = 'PASS'
        deployment_id = $deploymentId
        started_at = $startedAt.ToString('o')
        completed_at = [DateTime]::UtcNow.ToString('o')
        systemd = (Invoke-Wsl 'systemctl --version | head -1').Trim()
        checks = @('install and enable', 'private status publication', 'restart with stable repository state', 'clean SIGTERM stop', 'start after stop', 'unregister while preserving configuration and data')
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDirectory 'report.json') -Encoding utf8NoBOM
    Write-Host "PASS SYSTEMD: $serviceName lifecycle and persistence verified"
    Write-Host "Report: $reportDirectory"
} finally {
    try { Invoke-Wsl "/bin/sh '$linuxUninstaller' '$deploymentId'" -Root | Out-Null } catch {}
    try {
        Invoke-Wsl "case '$deploymentId' in ????????-????-????-????-????????????) rm -rf -- '$configurationRoot' '/var/lib/pixels/$deploymentId' '$validationToolRoot' ;; *) exit 9 ;; esac" -Root | Out-Null
    } catch {}
    if (Test-Path -LiteralPath $temporaryDirectory) { Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force }
}
