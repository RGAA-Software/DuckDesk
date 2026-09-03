param(
    [ValidateSet('rtc', 'rtc_direct')]
    [string]$ConnectionMode = 'rtc_direct',
    [ValidateSet('any', 'host', 'relay')]
    [string]$ExpectedCandidate = 'host',
    [string]$ConsoleBase = 'https://127.0.0.1:30500',
    [string]$TargetHost = '10.0.0.90',
    [string]$DeviceId = '001190520',
    [ValidateRange(12, 180)]
    [int]$ControllerSampleSeconds = 30,
    [ValidateRange(6, 120)]
    [int]$ObserverSampleSeconds = 9,
    [string]$EvidenceDir = ''
)

$ErrorActionPreference = 'Stop'

$caseScript = Join-Path $PSScriptRoot 'run_rtc_lan_case.ps1'
$powershell = (Get-Process -Id $PID).Path
$caseRoot = if ($EvidenceDir) { $EvidenceDir } else {
    Join-Path ([IO.Path]::GetTempPath()) ("px-rtc-multi-" + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null

function Start-Case([string]$Name, [string]$JoinMode, [int]$Seconds,
                    [string]$Confirmation = 'default', [int]$ConnectTimeoutSeconds = 30) {
    $stdout = Join-Path $caseRoot "$Name.stdout.log"
    $stderr = Join-Path $caseRoot "$Name.stderr.log"
    $evidence = Join-Path $caseRoot $Name
    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $caseScript,
        '-ConnectionMode', $ConnectionMode,
        '-JoinMode', $JoinMode,
        '-ExpectedCandidate', $ExpectedCandidate,
        '-SampleSeconds', $Seconds,
        '-ConnectTimeoutSeconds', $ConnectTimeoutSeconds,
        '-ConsoleBase', $ConsoleBase,
        '-TargetHost', $TargetHost,
        '-DeviceId', $DeviceId,
        '-TakeoverConfirmation', $Confirmation,
        '-EvidenceDir', $evidence
    )
    Start-Process -FilePath $powershell -ArgumentList $arguments -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
}

function Get-CaseLog([string]$Name) {
    $paths = @(
        (Join-Path $caseRoot "$Name.stdout.log"),
        (Join-Path $caseRoot "$Name.stderr.log")
    )
    (($paths | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object {
        Get-Content -LiteralPath $_ -Raw
    }) -join [Environment]::NewLine)
}

function Wait-ForLog([System.Diagnostics.Process]$Process, [string]$Name, [string]$Pattern, [int]$TimeoutSeconds = 45) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ((Get-CaseLog $Name) -match $Pattern) { return }
        if ($Process.HasExited) {
            throw "$Name exited before '$Pattern': $(Get-CaseLog $Name)"
        }
        Start-Sleep -Milliseconds 300
    }
    throw "$Name did not reach '$Pattern': $(Get-CaseLog $Name)"
}

function Wait-ForCase([System.Diagnostics.Process]$Process, [string]$Name, [int]$TimeoutSeconds = 120) {
    if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        throw "$Name timed out"
    }
    return Get-CaseLog $Name
}

function Assert-Pass([string]$Name, [string]$Log) {
    if ($Log -notmatch 'RESULT: PASS') {
        throw "$Name did not pass: $Log"
    }
}

$controller = $null
$takeoverController = $null
try {
    # One controller and one observer must remain connected concurrently. The
    # first controller stays alive while the observer fully completes its
    # sample; distinct short-lived Console users make the identities explicit.
    $controller = Start-Case 'controller_observer_controller' 'control' $ControllerSampleSeconds
    Wait-ForLog $controller 'controller_observer_controller' 'connected:\s+true'
    $observer = Start-Case 'controller_observer_observer' 'observe' $ObserverSampleSeconds
    $observerLog = Wait-ForCase $observer 'controller_observer_observer'
    Assert-Pass 'observer' $observerLog
    $controllerLog = Wait-ForCase $controller 'controller_observer_controller' ($ControllerSampleSeconds + 60)
    Assert-Pass 'controller' $controllerLog
    Write-Host 'MULTI_PHASE: controller-observer PASS'
    # A normal Controller close intentionally retains its lease for the
    # documented five-second reconnect window. Chromium completes its ICE
    # shutdown asynchronously after the diagnostic process exits, so include
    # that bounded teardown before starting the independent occupied/takeover
    # phase rather than mistaking a stale media peer for a product failure.
    Start-Sleep -Seconds 16

    # A second controller must receive the occupied response until its user
    # explicitly approves takeover. Do not accept a transport failure as a
    # substitute for the application-level occupied decision.
    $takeoverController = Start-Case 'takeover_original_controller' 'control' $ControllerSampleSeconds
    Wait-ForLog $takeoverController 'takeover_original_controller' 'connected:\s+true'
    $rejected = Start-Case 'takeover_rejected_controller' 'control' $ObserverSampleSeconds 'reject' 8
    $rejectedLog = Wait-ForCase $rejected 'takeover_rejected_controller'
    if ($rejected.ExitCode -eq 0 -or $rejectedLog -notmatch 'RTC_OCCUPIED_REJECTED') {
        throw "second controller was not explicitly rejected: $rejectedLog"
    }
    if ($takeoverController.HasExited) {
        throw "original controller ended before rejected contender completed: $((Get-CaseLog 'takeover_original_controller'))"
    }
    Write-Host 'MULTI_PHASE: second-controller-rejected PASS'

    # The only positive replacement path is the client confirmation. The
    # original Controller is deliberately demoted to Observer (not dropped),
    # while its old lease is invalidated server-side; the new one must complete
    # a healthy RTC sample with the sole controller lease.
    $accepted = Start-Case 'takeover_accepted_controller' 'control' $ObserverSampleSeconds 'accept'
    $acceptedLog = Wait-ForCase $accepted 'takeover_accepted_controller'
    Assert-Pass 'accepted takeover controller' $acceptedLog
    $originalLog = Wait-ForCase $takeoverController 'takeover_original_controller' ($ControllerSampleSeconds + 60)
    Assert-Pass 'demoted observer' $originalLog
    Write-Host 'MULTI_PHASE: explicit-takeover PASS'
    Write-Host "RESULT: PASS evidence=$caseRoot"
}
finally {
    foreach ($process in @($controller, $takeoverController) | Where-Object { $_ -and -not $_.HasExited }) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}
