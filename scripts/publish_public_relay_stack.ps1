#requires -Version 7.0

[CmdletBinding()]
param(
    [string]$ComputerName = '39.71.45.66',
    [ValidateRange(1, 65535)]
    [int]$RelayPort = 4605
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$consoleSource = Join-Path $repository '.cache/console-dev/release/px_console.exe'
$relaySource = Join-Path $repository '.cache/relay-dev/release/px_relay.exe'
$machineFile = Join-Path $repository '.env/test_machine.md'
$licenseFile = Join-Path $repository '.env/public_license.json'
foreach ($path in @($consoleSource, $relaySource, $machineFile, $licenseFile)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Public Relay deployment input is missing: $path"
    }
}

$machineText = Get-Content -LiteralPath $machineFile -Raw -Encoding UTF8
$password = [regex]::Match($machineText, '(?m)^\s*-\s*\u5bc6\u7801\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$machineName = [regex]::Match($machineText, '(?m)^\s*-\s*\u4e3b\u673a\u540d\s*[:\uff1a]\s*(.+?)\s*$').Groups[1].Value
$relayAppKey = [string](Get-Content -LiteralPath $licenseFile -Raw | ConvertFrom-Json).appkey
if (-not $password -or -not $machineName -or $relayAppKey.Length -lt 16) {
    throw 'Public Relay deployment credentials or app key are incomplete.'
}

$credential = [pscredential]::new(
    "$machineName\Administrator",
    (ConvertTo-SecureString $password -AsPlainText -Force)
)
$consoleHash = (Get-FileHash -LiteralPath $consoleSource -Algorithm SHA256).Hash
$relayHash = (Get-FileHash -LiteralPath $relaySource -Algorithm SHA256).Hash
$trustedHostsPath = 'WSMan:\localhost\Client\TrustedHosts'
$previousTrustedHosts = (Get-Item -LiteralPath $trustedHostsPath).Value
$session = $null
try {
    Set-Item -LiteralPath $trustedHostsPath -Value $ComputerName -Force
    $session = New-PSSession -ComputerName $ComputerName -Credential $credential
    Invoke-Command -Session $session -ScriptBlock {
        New-Item -ItemType Directory -Path 'D:\PixelsServer\relay' -Force | Out-Null
    }
    Copy-Item -LiteralPath $consoleSource -Destination 'D:\PixelsServer\app\bin\px_console.staged.exe' -ToSession $session -Force
    Copy-Item -LiteralPath $relaySource -Destination 'D:\PixelsServer\relay\px_relay.staged.exe' -ToSession $session -Force
    Invoke-Command -Session $session -ArgumentList $ComputerName, $RelayPort, $relayAppKey, $consoleHash, $relayHash -ScriptBlock {
        param($publicHost, $relayPort, $relayAppKey, $consoleHash, $relayHash)

        $ErrorActionPreference = 'Stop'
        $serverRoot = 'D:\PixelsServer'
        $consolePath = "$serverRoot\app\bin\px_console.exe"
        $consoleStaged = "$serverRoot\app\bin\px_console.staged.exe"
        $consoleLauncher = "$serverRoot\config\start-console.ps1"
        $relayDirectory = "$serverRoot\relay"
        $relayPath = "$relayDirectory\px_relay.exe"
        $relayStaged = "$relayDirectory\px_relay.staged.exe"
        $relayLauncher = "$relayDirectory\start-relay.ps1"
        foreach ($artifact in @(@($consoleStaged, $consoleHash), @($relayStaged, $relayHash))) {
            if ((Get-FileHash -LiteralPath $artifact[0] -Algorithm SHA256).Hash -ne $artifact[1]) {
                throw "Staged artifact hash mismatch: $($artifact[0])"
            }
        }

        $timestamp = [DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
        $backupDirectory = "$serverRoot\backups\relay-stack-before-$timestamp"
        New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
        if (Get-ScheduledTask -TaskName 'Pixels-Relay' -ErrorAction SilentlyContinue) {
            Stop-ScheduledTask -TaskName 'Pixels-Relay' -ErrorAction SilentlyContinue
        }
        Stop-ScheduledTask -TaskName 'Pixels-Console' -ErrorAction SilentlyContinue
        Get-CimInstance Win32_Process |
            Where-Object { $_.ExecutablePath -in @($consolePath, $relayPath) } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
        foreach ($processName in @('px_console', 'px_relay')) {
            for ($attempt = 0; $attempt -lt 40 -and (Get-Process -Name $processName -ErrorAction SilentlyContinue); $attempt++) {
                Start-Sleep -Milliseconds 250
            }
        }

        if (Test-Path -LiteralPath $consolePath -PathType Leaf) {
            Copy-Item -LiteralPath $consolePath -Destination "$backupDirectory\px_console.exe" -Force
        }
        Copy-Item -LiteralPath $consoleLauncher -Destination "$backupDirectory\start-console.ps1" -Force
        if (Test-Path -LiteralPath $relayPath -PathType Leaf) {
            Copy-Item -LiteralPath $relayPath -Destination "$backupDirectory\px_relay.exe" -Force
        }
        if (Test-Path -LiteralPath $relayLauncher -PathType Leaf) {
            Copy-Item -LiteralPath $relayLauncher -Destination "$backupDirectory\start-relay.ps1" -Force
        }

        Copy-Item -LiteralPath $consoleStaged -Destination $consolePath -Force
        Copy-Item -LiteralPath $relayStaged -Destination $relayPath -Force
        Remove-Item -LiteralPath $consoleStaged, $relayStaged -Force
        if ((Get-FileHash -LiteralPath $consolePath -Algorithm SHA256).Hash -ne $consoleHash -or
            (Get-FileHash -LiteralPath $relayPath -Algorithm SHA256).Hash -ne $relayHash) {
            throw 'Installed Relay stack hash mismatch.'
        }

        $consoleText = Get-Content -LiteralPath $consoleLauncher -Raw
        $consoleText = [regex]::Replace(
            $consoleText,
            '(?m)^\s*\$env:PIXELS_RELAY_(?:PUBLIC_HOST|PUBLIC_PORT|APP_KEY)\s*=.*(?:\r?\n)?',
            '')
        $escapedHost = $publicHost.Replace("'", "''")
        $escapedAppKey = $relayAppKey.Replace("'", "''")
        $relayEnvironment = @"
`$env:PIXELS_RELAY_PUBLIC_HOST = '$escapedHost'
`$env:PIXELS_RELAY_PUBLIC_PORT = '$relayPort'
`$env:PIXELS_RELAY_APP_KEY = '$escapedAppKey'

"@
        if ($consoleText -notmatch '(?m)^\$consoleProcess\s*=') {
            throw 'Console launcher structure is not recognized.'
        }
        $consoleText = [regex]::Replace($consoleText, '(?m)^(\$consoleProcess\s*=)', $relayEnvironment + '$1', 1)
        [IO.File]::WriteAllText($consoleLauncher, $consoleText, [Text.UTF8Encoding]::new($false))

        $relayScript = @"
`$ErrorActionPreference = 'Stop'
`$env:PIXELS_RELAY_LISTEN = '0.0.0.0:$relayPort'
`$env:PIXELS_RELAY_APP_KEY = '$escapedAppKey'
`$env:PIXELS_RELAY_MAX_CONNECTIONS = '4096'
`$env:PIXELS_RELAY_MAX_ROOMS = '2048'
`$env:PIXELS_RELAY_OUTBOUND_QUEUE = '256'
`$env:PIXELS_RELAY_MAX_MESSAGE_BYTES = '8388608'
`$env:RUST_LOG = 'px_relay_server=info'
`$process = Start-Process -FilePath '$relayPath' -NoNewWindow -PassThru -Wait -RedirectStandardOutput '$serverRoot\logs\relay.stdout.log' -RedirectStandardError '$serverRoot\logs\relay.stderr.log'
exit `$process.ExitCode
"@
        [IO.File]::WriteAllText($relayLauncher, $relayScript, [Text.UTF8Encoding]::new($false))

        $relayAcl = New-Object Security.AccessControl.FileSecurity
        $relayAcl.SetAccessRuleProtection($true, $false)
        foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
            $identity = [Security.Principal.SecurityIdentifier]::new($sid)
            $rule = [Security.AccessControl.FileSystemAccessRule]::new(
                $identity,
                [Security.AccessControl.FileSystemRights]::FullControl,
                [Security.AccessControl.AccessControlType]::Allow)
            $relayAcl.AddAccessRule($rule)
        }
        Set-Acl -LiteralPath $relayLauncher -AclObject $relayAcl

        $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument (
            '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $relayLauncher + '"')
        $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -RestartCount 5 -RestartInterval (New-TimeSpan -Minutes 1)
        $trigger = New-ScheduledTaskTrigger -AtStartup
        Register-ScheduledTask -TaskName 'Pixels-Relay' -Action $action -Principal $principal -Settings $settings -Trigger $trigger -Force | Out-Null
        if (-not (Get-NetFirewallRule -DisplayName 'Pixels Relay TCP' -ErrorAction SilentlyContinue)) {
            New-NetFirewallRule -DisplayName 'Pixels Relay TCP' -Direction Inbound -Action Allow -Protocol TCP -LocalPort $relayPort | Out-Null
        }

        Start-ScheduledTask -TaskName 'Pixels-Relay'
        $relayReady = $false
        for ($attempt = 0; $attempt -lt 40; $attempt++) {
            Start-Sleep -Milliseconds 250
            if (Test-NetConnection -ComputerName '127.0.0.1' -Port $relayPort -InformationLevel Quiet -WarningAction SilentlyContinue) {
                $relayReady = $true
                break
            }
        }
        if (-not $relayReady) {
            throw 'Relay did not become reachable.'
        }
        $health = Invoke-RestMethod -Uri "http://127.0.0.1:$relayPort/healthz" -TimeoutSec 5
        if ($health.status -ne 'ok') {
            throw 'Relay health endpoint did not report ok.'
        }

        Start-ScheduledTask -TaskName 'Pixels-Console'
        $consoleReady = $false
        for ($attempt = 0; $attempt -lt 80; $attempt++) {
            Start-Sleep -Milliseconds 250
            if (Test-NetConnection -ComputerName '127.0.0.1' -Port 4600 -InformationLevel Quiet -WarningAction SilentlyContinue) {
                $consoleReady = $true
                break
            }
        }
        if (-not $consoleReady) {
            throw 'Console did not become reachable after Relay configuration.'
        }

        [pscustomobject]@{
            ConsoleHash = (Get-FileHash -LiteralPath $consolePath -Algorithm SHA256).Hash
            RelayHash = (Get-FileHash -LiteralPath $relayPath -Algorithm SHA256).Hash
            ConsoleTask = (Get-ScheduledTask -TaskName 'Pixels-Console').State.ToString()
            RelayTask = (Get-ScheduledTask -TaskName 'Pixels-Relay').State.ToString()
            RelayConnections = [int]$health.connections
            RelayRooms = [int]$health.rooms
            RecoverableBackup = $backupDirectory
        }
    }
}
finally {
    if ($session) {
        Remove-PSSession $session
    }
    Set-Item -LiteralPath $trustedHostsPath -Value $previousTrustedHosts -Force
    $relayAppKey = $null
    $password = $null
}
