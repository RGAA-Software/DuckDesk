#requires -Version 7.0
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$deploymentId = [guid]::NewGuid().ToString()
$serviceName = "pixels-console@$deploymentId.service"
$linuxRepositoryRoot = '/mnt/d/GoCloud/GammaRayPremium'
$linuxInstaller = "$linuxRepositoryRoot/scripts/server_console/install_linux_service.sh"
$linuxUninstaller = "$linuxRepositoryRoot/scripts/server_console/uninstall_linux_service.sh"
$fixtureRoot = "/tmp/pixels-console-systemd-$deploymentId"
$sourceBinary = "$fixtureRoot/px_console"
$sourceEnvironment = "$fixtureRoot/console.env"
$probeDirectory = "/var/lib/pixels/$deploymentId/console"
$configurationRoot = "/etc/pixels/$deploymentId"
$installedBinary = '/opt/pixels/current/bin/px_console'
$installedUnit = '/etc/systemd/system/pixels-console@.service'
$preservedBinary = "$fixtureRoot/preserved-px_console"
$preservedUnit = "$fixtureRoot/preserved-pixels-console.service"
$startedAt = [DateTime]::UtcNow

function Invoke-Wsl([string]$Command, [switch]$Root) {
    $arguments = @()
    if ($Root) {
        $arguments += @('-u', 'root')
    }
    $arguments += @(
        '-e', 'env', '-i',
        'HOME=/home/chessplayer',
        'PATH=/home/chessplayer/.cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin',
        '/bin/bash', '--noprofile', '--norc', '-lc', $Command
    )
    $commandOutput = & wsl @arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "WSL command failed: $Command`n$($commandOutput -join "`n")"
    }
    return ($commandOutput -join "`n")
}

function Invoke-WslExpectedFailure([string]$Command) {
    & wsl -u root -e env -i `
        'HOME=/root' `
        'PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin' `
        /bin/bash --noprofile --norc -lc $Command *> $null
    if ($LASTEXITCODE -eq 0) {
        throw "WSL command unexpectedly succeeded: $Command"
    }
}

function Write-LinuxFixture([string]$Path, [string]$Contents, [string]$Mode) {
    $encodedContents = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Contents))
    Invoke-Wsl "printf '%s' '$encodedContents' | base64 -d > '$Path'; chmod '$Mode' '$Path'" -Root | Out-Null
}

function New-ProbeBinary([int]$Revision) {
    $binaryContents = @"
#!/bin/sh
set -eu
probe_file="`${PIXELS_SYSTEMD_PROBE_PATH}/lifecycle.log"
trap 'printf "stopped-v$Revision\n" >> "`$probe_file"; exit 0' TERM INT
printf "started-v$Revision uid=%s\n" "`$(id -u)" >> "`$probe_file"
while :; do
    sleep 1
done
"@
    Write-LinuxFixture -Path $sourceBinary -Contents $binaryContents -Mode '0700'
}

try {
    $pidOne = Invoke-Wsl 'ps -p 1 -o comm='
    if ($pidOne.Trim() -ne 'systemd') {
        throw "WSL PID 1 is not systemd: $pidOne"
    }

    Invoke-Wsl "install -d -o root -g root -m 0700 '$fixtureRoot'; if [ -f '$installedBinary' ]; then cp -a '$installedBinary' '$preservedBinary'; fi; if [ -f '$installedUnit' ]; then cp -a '$installedUnit' '$preservedUnit'; fi" -Root | Out-Null
    New-ProbeBinary -Revision 1
    $environmentContents = "PIXELS_DEPLOYMENT_ID=$deploymentId`nPIXELS_SYSTEMD_PROBE_PATH=$probeDirectory`n"
    Write-LinuxFixture -Path $sourceEnvironment -Contents $environmentContents -Mode '0600'

    Invoke-WslExpectedFailure "/bin/sh '$linuxInstaller' '$($deploymentId.ToUpperInvariant())' '$sourceBinary' '$sourceEnvironment'"
    Write-LinuxFixture -Path $sourceEnvironment -Contents $environmentContents -Mode '0644'
    Invoke-WslExpectedFailure "/bin/sh '$linuxInstaller' '$deploymentId' '$sourceBinary' '$sourceEnvironment'"
    Write-LinuxFixture -Path $sourceEnvironment -Contents "PIXELS_DEPLOYMENT_ID=$([guid]::NewGuid().ToString())`nPIXELS_SYSTEMD_PROBE_PATH=$probeDirectory`n" -Mode '0600'
    Invoke-WslExpectedFailure "/bin/sh '$linuxInstaller' '$deploymentId' '$sourceBinary' '$sourceEnvironment'"
    Write-LinuxFixture -Path $sourceEnvironment -Contents $environmentContents -Mode '0600'

    Invoke-Wsl "/bin/sh '$linuxInstaller' '$deploymentId' '$sourceBinary' '$sourceEnvironment'" -Root | Out-Null
    if ((Invoke-Wsl "systemctl is-active '$serviceName'" -Root).Trim() -ne 'active') {
        throw 'Console systemd service did not become active'
    }
    $serviceUser = (Invoke-Wsl "main_pid=`$(systemctl show '$serviceName' --property=MainPID --value); ps -o user= -p `$main_pid" -Root).Trim()
    if ($serviceUser -ne 'pixels-console') {
        throw "Console systemd service ran as an unexpected user: $serviceUser"
    }
    $environmentMode = (Invoke-Wsl "stat -c '%U:%G:%a' '$configurationRoot/console.env'" -Root).Trim()
    if ($environmentMode -ne 'pixels-console:pixels-console:400') {
        throw "Installed Console environment permissions are invalid: $environmentMode"
    }
    $firstProbe = Invoke-Wsl "cat '$probeDirectory/lifecycle.log'" -Root
    if ($firstProbe -notmatch '(?m)^started-v1 uid=') {
        throw "Initial Console service did not execute the installed binary:`n$firstProbe"
    }

    New-ProbeBinary -Revision 2
    Invoke-Wsl "/bin/sh '$linuxInstaller' '$deploymentId' '$sourceBinary' '$sourceEnvironment'" -Root | Out-Null
    $upgradeProbe = Invoke-Wsl "cat '$probeDirectory/lifecycle.log'" -Root
    if ($upgradeProbe -notmatch '(?m)^stopped-v1$' -or $upgradeProbe -notmatch '(?m)^started-v2 uid=') {
        throw "Console in-place service upgrade did not stop and replace the binary:`n$upgradeProbe"
    }

    Invoke-Wsl "systemctl restart '$serviceName'" -Root | Out-Null
    $restartProbe = Invoke-Wsl "cat '$probeDirectory/lifecycle.log'" -Root
    if (@($restartProbe -split "`n" | Where-Object { $_ -match '^started-v2 uid=' }).Count -lt 2) {
        throw "Console service did not restart the replacement binary:`n$restartProbe"
    }

    Invoke-Wsl "systemctl stop '$serviceName'" -Root | Out-Null
    $stopResult = Invoke-Wsl "systemctl show '$serviceName' --property=ActiveState --property=Result --property=ExecMainStatus --value" -Root
    if ($stopResult -notmatch 'inactive' -or $stopResult -notmatch '(?m)^success$' -or $stopResult -notmatch '(?m)^0$') {
        throw "Console SIGTERM stop was not clean:`n$stopResult"
    }
    Invoke-Wsl "systemctl start '$serviceName' && systemctl is-active --quiet '$serviceName'" -Root | Out-Null
    Invoke-Wsl "/bin/sh '$linuxUninstaller' '$deploymentId'" -Root | Out-Null
    if ((Invoke-Wsl "systemctl is-enabled '$serviceName' 2>/dev/null || true" -Root).Trim() -eq 'enabled') {
        throw 'Console uninstall left the deployment instance enabled'
    }
    Invoke-Wsl "test -f '$configurationRoot/console.env' && test -f '$probeDirectory/lifecycle.log'" -Root | Out-Null

    $reportDirectory = Join-Path $repositoryRoot "test-results/server_validation/console-systemd-$deploymentId"
    New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
    [ordered]@{
        status = 'PASS'
        deployment_id = $deploymentId
        started_at = $startedAt.ToString('o')
        completed_at = [DateTime]::UtcNow.ToString('o')
        systemd = (Invoke-Wsl 'systemctl --version | head -1').Trim()
        scope = 'WSL2 systemd installer lifecycle with a signal-aware validation executable; actual px_console Unix SIGTERM/restart/database-authority behavior is covered separately by the console-process suite.'
        checks = @(
            'uppercase deployment rejected',
            'over-permissive environment rejected',
            'deployment mismatch rejected',
            'private environment installed as mode 0400',
            'dedicated service identity',
            'in-place binary replacement',
            'systemd restart',
            'clean SIGTERM stop',
            'unregister while preserving private environment and runtime data'
        )
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDirectory 'report.json') -Encoding utf8NoBOM
    Write-Host "PASS CONSOLE SYSTEMD: $serviceName installation, upgrade, lifecycle and persistence verified"
    Write-Host "Report: $reportDirectory"
} finally {
    try {
        Invoke-Wsl "/bin/sh '$linuxUninstaller' '$deploymentId'" -Root | Out-Null
    } catch {}
    try {
        Invoke-Wsl "if [ -f '$preservedBinary' ]; then install -o root -g root -m 0755 '$preservedBinary' '$installedBinary'; else rm -f -- '$installedBinary'; fi; if [ -f '$preservedUnit' ]; then install -o root -g root -m 0644 '$preservedUnit' '$installedUnit'; else rm -f -- '$installedUnit'; fi; systemctl daemon-reload; case '$deploymentId' in ????????-????-????-????-????????????) rm -rf -- '$configurationRoot' '/var/lib/pixels/$deploymentId' '$fixtureRoot' ;; *) exit 9 ;; esac" -Root | Out-Null
    } catch {}
}
