[CmdletBinding()]
param(
    [string]$ConsoleUrl = 'https://127.0.0.1:30500',
    [string]$ConsoleExe = (Join-Path $PSScriptRoot '..\output\px_console\px_console.exe'),
    [switch]$AllowRestart
)

$ErrorActionPreference = 'Stop'
if (-not $AllowRestart) {
    throw 'This live test restarts px_console. Re-run with -AllowRestart.'
}

$consolePath = [IO.Path]::GetFullPath($ConsoleExe)
$consoleDirectory = [IO.Path]::GetDirectoryName($consolePath)
if (-not (Test-Path -LiteralPath $consolePath -PathType Leaf)) {
    throw "px_console executable not found: $consolePath"
}
if (-not $consolePath.StartsWith([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')), [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to restart a Console outside this workspace: $consolePath"
}

function Wait-ConsoleHealth {
    param([int]$TimeoutSeconds = 40)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        try {
            $response = Invoke-WebRequest -Uri "$ConsoleUrl/health" -SkipCertificateCheck -TimeoutSec 3
            if ($response.StatusCode -eq 200) { return }
        }
        catch {}
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "Console health check did not recover within ${TimeoutSeconds}s"
}

function Get-WorkspaceConsoleProcesses {
    @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -eq 'px_console.exe' -and
        $_.ExecutablePath -and
        [IO.Path]::GetFullPath($_.ExecutablePath).Equals($consolePath, [StringComparison]::OrdinalIgnoreCase)
    })
}

function Stop-WorkspaceConsole {
    $deadline = (Get-Date).AddSeconds(8)
    do {
        $processes = Get-WorkspaceConsoleProcesses
        foreach ($process in $processes) {
            Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
        }
        if ($processes.Count -eq 0) { return }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    $remaining = Get-WorkspaceConsoleProcesses
    if ($remaining.Count -gt 0) {
        throw "Unable to stop workspace Console processes: $($remaining.ProcessId -join ',')"
    }
}

function Start-WorkspaceConsole {
    if ((Get-WorkspaceConsoleProcesses).Count -eq 0) {
        Start-Process -FilePath $consolePath -WorkingDirectory $consoleDirectory -WindowStyle Hidden
    }
    Wait-ConsoleHealth
}

function Invoke-ConsoleJson {
    param(
        [Parameter(Mandatory)][string]$Path,
        [ValidateSet('GET', 'POST')][string]$Method = 'GET',
        [Microsoft.PowerShell.Commands.WebRequestSession]$Session,
        [hashtable]$Headers = @{},
        [object]$Body
    )
    $parameters = @{
        Uri = "$ConsoleUrl$Path"
        Method = $Method
        SkipCertificateCheck = $true
        TimeoutSec = 35
        Headers = $Headers
    }
    if ($Session) { $parameters.WebSession = $Session }
    if ($null -ne $Body) {
        $parameters.ContentType = 'application/json'
        $parameters.Body = ($Body | ConvertTo-Json -Depth 8 -Compress)
    }
    Invoke-RestMethod @parameters
}

$instanceId = ''
$csrf = ''
$testRenderIds = @()
$session = [Microsoft.PowerShell.Commands.WebRequestSession]::new()
$restartCompleted = $false
try {
    Wait-ConsoleHealth
    $guestNonce = [Guid]::NewGuid().ToString('N')
    $guest = Invoke-ConsoleJson -Path '/api/v1/session/guest' -Method POST -Session $session `
        -Headers @{ Origin = $ConsoleUrl } -Body @{ client_nonce = $guestNonce }
    $csrf = [string]$guest.data.csrf_token
    if ([string]::IsNullOrWhiteSpace($csrf)) { throw 'Guest session did not return a CSRF token' }

    $catalog = Invoke-ConsoleJson -Path '/api/v1/public/apps'
    $app = @($catalog.data)[0]
    if (-not $app) { throw 'No public application is available for restart recovery testing' }

    $baselineRenderIds = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -eq 'px_render.exe' -and $_.CommandLine -match '--app_mode=game-hook'
    } | ForEach-Object ProcessId)
    $startNonce = [Guid]::NewGuid().ToString('N')
    $started = Invoke-ConsoleJson -Path "/api/v1/public/apps/$([Uri]::EscapeDataString($app.app_id))/start" `
        -Method POST -Session $session -Headers @{ Origin = $ConsoleUrl; 'X-CSRF-Token' = $csrf } `
        -Body @{ client_nonce = $startNonce }
    $instanceId = [string]$started.data.instance_id
    if ([string]::IsNullOrWhiteSpace($instanceId) -or $started.data.state -ne 'running') {
        throw "Application did not reach running before restart: $($started.data.state)"
    }
    $testRenderIds = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -eq 'px_render.exe' -and
        $_.CommandLine -match '--app_mode=game-hook' -and
        $_.ProcessId -notin $baselineRenderIds
    } | ForEach-Object ProcessId)
    Write-Host "Started public instance $instanceId; restarting Console..."

    Stop-WorkspaceConsole
    Start-WorkspaceConsole
    $restartCompleted = $true
    # HTTPS can become healthy before px_service has re-established its
    # WebSocket. Give the control plane a short window so the cleanup command
    # is not mistaken for an offline-device stop.
    Start-Sleep -Seconds 3

    $deadline = (Get-Date).AddSeconds(35)
    $restored = $null
    do {
        try {
            $instances = Invoke-ConsoleJson -Path '/api/v1/public/instances' -Session $session
            $restored = @($instances.data) | Where-Object { $_.instance_id -eq $instanceId } | Select-Object -First 1
            if ($restored -and $restored.state -eq 'running') { break }
        }
        catch {}
        Start-Sleep -Milliseconds 750
    } while ((Get-Date) -lt $deadline)
    if (-not $restored -or $restored.state -ne 'running') {
        throw "Instance $instanceId was not restored as running after Console restart"
    }

    $ticketNonce = [Guid]::NewGuid().ToString('N')
    $ticket = Invoke-ConsoleJson -Path "/api/v1/public/instances/$([Uri]::EscapeDataString($instanceId))/ticket" `
        -Method POST -Session $session -Headers @{ Origin = $ConsoleUrl; 'X-CSRF-Token' = $csrf } `
        -Body @{ client_nonce = $ticketNonce; join_mode = 'observe' }
    if ([string]::IsNullOrWhiteSpace([string]$ticket.data.ticket)) {
        throw 'Restored instance did not issue a new connection ticket'
    }
    $permissions = @($ticket.data.permissions)
    if ($permissions.Count -ne 2 -or $permissions[0] -ne 'view' -or $permissions[1] -ne 'audio') {
        throw "Restored instance returned unexpected ticket permissions: $($ticket.data.permissions -join ',')"
    }

    Write-Host "PASS: instance $instanceId remained running and issued an observer ticket after Console restart."
}
finally {
    if (-not $restartCompleted) {
        Start-WorkspaceConsole
    }
    if ($instanceId -and $csrf) {
        $cleaned = $false
        $lastCleanupError = ''
        for ($attempt = 1; $attempt -le 8 -and -not $cleaned; $attempt++) {
            try {
                Invoke-ConsoleJson -Path "/api/v1/public/instances/$([Uri]::EscapeDataString($instanceId))/stop" `
                    -Method POST -Session $session -Headers @{ Origin = $ConsoleUrl; 'X-CSRF-Token' = $csrf } `
                    -Body @{ reason = 'console_restart_recovery_test_cleanup' } | Out-Null
                $cleaned = $true
            }
            catch {
                $lastCleanupError = $_.Exception.Message
                Start-Sleep -Seconds 1
            }
        }
        if (-not $cleaned) {
            # A first offline stop can already place the row in a terminal
            # state. Verify that no physical game-hook Render survived before
            # reporting cleanup failure.
            $gameRender = Get-CimInstance Win32_Process | Where-Object {
                $_.Name -eq 'px_render.exe' -and $_.ProcessId -in $testRenderIds
            }
            if ($gameRender) {
                Write-Warning "Failed to clean up test instance ${instanceId}: $lastCleanupError"
            }
        }
    }
}
