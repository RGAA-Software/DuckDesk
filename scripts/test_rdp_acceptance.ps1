#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][uri]$ConsoleUrl,
    [Parameter(Mandatory)][string]$ConsoleCaPem,
    [Parameter(Mandatory)][string]$AppId,
    [string]$ClientExe = (Join-Path $PSScriptRoot '../build_official/dist/px_client.exe'),
    [ValidateSet('Connect', 'GraceExit', 'GraceReconnect', 'RevokeSession', 'WrongCertificate', 'WrongAccount', 'WrongPassword')][string]$Scenario = 'Connect',
    [ValidateRange(10, 60)][int]$ObserveSeconds = 25,
    # Optional DPAPI-protected credential for cleanup after the test revokes its own guest token.
    [string]$CleanupAdminCredentialFile = ''
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or $ConsoleUrl.Scheme -ne 'https' -or $AppId -notmatch '^[a-zA-Z0-9_-]{1,128}$') {
    throw 'Windows, a trusted HTTPS Console URL and a valid application ID are required'
}
$clientPath = (Resolve-Path -LiteralPath $ClientExe).Path
$cleanupCredential = $null
if ($Scenario -eq 'RevokeSession') {
    if (-not $CleanupAdminCredentialFile) { throw 'RevokeSession requires a private cleanup credential file' }
    $cleanupCredential = Import-Clixml -LiteralPath $CleanupAdminCredentialFile
    if ($cleanupCredential -isnot [pscredential]) { throw 'Cleanup credential must be a DPAPI-protected PSCredential' }
}
# This probe uses only an explicitly selected, public acceptance app. It never
# logs on Administrator, edits account credentials or logs off Windows sessions.
# Secrets travel in memory/stdin, never in command-line arguments or report files.
if (-not ('GammaRay.RdpAcceptance' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Net.Security;
using System.Security.Cryptography.X509Certificates;
using System.Threading;
namespace GammaRay {
public sealed class RdpAcceptance : IDisposable {
    private readonly X509Certificate2 ca;
    private readonly object gate = new object();
    private Process child;
    private Timer timer;
    public HttpClient Http { get; }
    public RdpAcceptance(string path) {
        ca = X509Certificate2.CreateFromPem(File.ReadAllText(path));
        var handler = new HttpClientHandler();
        handler.ServerCertificateCustomValidationCallback = (_, cert, chain, errors) => {
            if (cert == null || chain == null ||
                (errors & (SslPolicyErrors.RemoteCertificateNameMismatch | SslPolicyErrors.RemoteCertificateNotAvailable)) != 0) return false;
            chain.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
            chain.ChainPolicy.CustomTrustStore.Clear();
            chain.ChainPolicy.CustomTrustStore.Add(ca);
            // Private deployment CA has no public revocation endpoint.
            chain.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
            return chain.Build(cert);
        };
        Http = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(35) };
    }
    public void Guard(Process process) {
        lock (gate) {
            timer?.Dispose();
            child = process;
            timer = new Timer(_ => {
                lock (gate) { try { if (child != null && !child.HasExited) child.Kill(); } catch {} }
            }, null, 180000, Timeout.Infinite);
        }
    }
    public void Dispose() {
        lock (gate) { timer?.Dispose(); child = null; }
        Http.Dispose(); ca.Dispose();
    }
}}
'@
}
$owner = [GammaRay.RdpAcceptance]::new((Resolve-Path -LiteralPath $ConsoleCaPem).Path)
function Invoke-ProbeApi([string]$Path, [object]$Body, [string]$Bearer = '', [int]$TimeoutSeconds = 35, [string]$Csrf = '') {
    $method = if ($null -eq $Body) { [Net.Http.HttpMethod]::Get } else { [Net.Http.HttpMethod]::Post }
    $request = [Net.Http.HttpRequestMessage]::new($method, [uri]::new($ConsoleUrl, $Path))
    $timeout = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($TimeoutSeconds))
    $response = $null
    try {
        $request.Headers.Add('Origin', $ConsoleUrl.GetLeftPart([UriPartial]::Authority))
        if ($Bearer) { $request.Headers.Authorization = [Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $Bearer) }
        if ($Csrf) { $request.Headers.Add('X-CSRF-Token', $Csrf) }
        if ($null -ne $Body) {
            $request.Content = [Net.Http.StringContent]::new(($Body | ConvertTo-Json -Depth 15 -Compress), [Text.Encoding]::UTF8, 'application/json')
        }
        $response = $owner.Http.SendAsync($request, $timeout.Token).GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) { throw "Acceptance API $Path HTTP $([int]$response.StatusCode)" }
        $result = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json
        if ($result.code -and $result.code -ne 200) { throw "Acceptance API code $($result.code)" }
        return $result.data
    } finally { if ($response) { $response.Dispose() }; $request.Dispose(); $timeout.Dispose() }
}
$startedAt = [DateTime]::UtcNow
$instance = $null
$client = $null
$guest = $null
$presented = $false
$rejected = $false
$normalExit = $false
$codecErrors = 0
$connectionErrors = @()
$graceExited = $false
$terminalState = ''
$reconnected = $false
$recovery = $null
$lastTitle = ''
$revoked = $false
$revocationClosed = $false
$revocationSeconds = $null
try {
    $guest = Invoke-ProbeApi '/api/v1/session/guest' @{client_nonce = [guid]::NewGuid().ToString('N'); client_type = 'panel'}
    $instance = Invoke-ProbeApi "/api/v1/public/apps/$AppId/start" @{client_nonce = [guid]::NewGuid().ToString('N')} $guest.access_token
    if ($instance.state -ne 'running') { throw 'Acceptance instance failed to become ready' }
    $nonce = [guid]::NewGuid().ToString('N')
    $ticket = Invoke-ProbeApi "/api/v1/public/instances/$($instance.instance_id)/ticket" @{
        client_nonce = $nonce; join_mode = 'control'; client_capability = 'windows-rdp-v1'
    } $guest.access_token
    if (-not $ticket.rdp) { throw 'RDP capability was not issued' }
    if ($Scenario -eq 'GraceReconnect') {
        $recovery = @{renewal_token = $ticket.renewal_token; nonce = $nonce; rdp = $ticket.rdp;
            logical_session_id = $ticket.logical_session_id; launch_url = $ticket.launch_url}
    }
    switch ($Scenario) {
        'WrongCertificate' { $ticket.rdp.proxy_certificate_sha256 = '0' * 64 }
        'WrongAccount' { $ticket.rdp.account_name = 'grdp_invalidprobe' }
        'WrongPassword' { $ticket.rdp.password = 'aA1!' + [guid]::NewGuid().ToString('N') }
    }
    $route = [uri]$ticket.launch_url
    $handoff = @{
        schema = 1; host = $route.Host; port = $route.Port; instance_id = $instance.instance_id; device_id = $ticket.rdp.device_id
        stream_id = $ticket.stream_id; ticket = $ticket.ticket; nonce = $nonce; visitor_id = 'accept-' + [guid]::NewGuid().ToString('N')
        audio = $true; clipboard = $true; rdp = $ticket.rdp
    }
    $start = [Diagnostics.ProcessStartInfo]::new($clientPath)
    $start.ArgumentList.Add('--rdp-launch-stdin')
    $start.WorkingDirectory = Split-Path $clientPath -Parent
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $client = [Diagnostics.Process]::new(); $client.StartInfo = $start
    [void]$client.Start(); $owner.Guard($client)
    Write-Host "RDP_ACCEPTANCE_INSTANCE=$($instance.instance_id) CLIENT_PID=$($client.Id)"
    $stdout = $client.StandardOutput.ReadToEndAsync(); $stderr = $client.StandardError.ReadToEndAsync()
    $client.StandardInput.Write(($handoff | ConvertTo-Json -Depth 15 -Compress)); $client.StandardInput.Close()
    $handoff = $null; $ticket = $null
    $deadline = [DateTime]::UtcNow.AddSeconds($ObserveSeconds)
    do {
        $client.Refresh()
        $lastTitle = $client.MainWindowTitle
        $presented = $presented -or $client.MainWindowTitle -eq 'GammaRay · RDP 工作区'
        if ($client.MainWindowTitle -match '失败|已关闭|已断开|拒绝|超时|授权无效|已被占用') { $rejected = $true; break }
        if ($client.HasExited) { $rejected = $true; break }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($Scenario -eq 'RevokeSession' -and $presented -and -not $rejected) {
        # Revoke only the guest created by this invocation, never the operator's session.
        [void](Invoke-ProbeApi '/api/v1/session/user/logout' @{} $guest.access_token)
        $revoked = $true
        $revokedAt = [DateTime]::UtcNow
        do {
            Start-Sleep -Milliseconds 200
            $client.Refresh()
            $lastTitle = $client.MainWindowTitle
            $revocationClosed = $client.HasExited -or $lastTitle -match '失败|已关闭|已断开|拒绝|授权无效'
        } while (-not $revocationClosed -and [DateTime]::UtcNow -lt $revokedAt.AddSeconds(30))
        $revocationSeconds = [Math]::Round(([DateTime]::UtcNow - $revokedAt).TotalSeconds, 2)
    }
    if ($Scenario -eq 'GraceReconnect' -and $presented -and -not $rejected) {
        # Rotate the original recovery capability, preserving its logical owner.
        # Issuing an unrelated ticket creates a different seat and must be rejected.
        $nonce = $recovery.nonce
        $ticket = Invoke-ProbeApi '/api/v1/connection-tickets/renew' @{
            client_nonce = $nonce; renewal_token = $recovery.renewal_token
        }
        if ($ticket.logical_session_id -ne $recovery.logical_session_id) { throw 'Renewal changed the logical owner' }
        $route = [uri]$recovery.launch_url
        $handoff = @{
            schema = 1; host = $route.Host; port = $route.Port; instance_id = $instance.instance_id; device_id = $recovery.rdp.device_id
            stream_id = $ticket.stream_id; ticket = $ticket.ticket; nonce = $nonce; visitor_id = 'reconnect-' + [guid]::NewGuid().ToString('N')
            audio = $true; clipboard = $true; rdp = $recovery.rdp
        }
        [void]$client.CloseMainWindow()
        if (-not $client.WaitForExit(3000) -or $client.ExitCode -ne 0) { throw 'First client failed to exit before reconnect' }
        if ($stderr.Wait(1000)) {
            $codecErrors += [regex]::Matches($stderr.Result, 'decompress failed|decompress failure|DecodeFrame2 state|YUV.*failed|yuv.*failed').Count
        }
        $client.Dispose(); $client = $null
        Start-Sleep -Milliseconds 300
        $client = [Diagnostics.Process]::new(); $client.StartInfo = $start
        [void]$client.Start(); $owner.Guard($client)
        Write-Host "RDP_ACCEPTANCE_RECONNECT_INSTANCE=$($instance.instance_id) CLIENT_PID=$($client.Id)"
        $stdout = $client.StandardOutput.ReadToEndAsync(); $stderr = $client.StandardError.ReadToEndAsync()
        $client.StandardInput.Write(($handoff | ConvertTo-Json -Depth 15 -Compress)); $client.StandardInput.Close()
        $handoff = $null; $ticket = $null; $recovery = $null
        $deadline = [DateTime]::UtcNow.AddSeconds($ObserveSeconds)
        do {
            $client.Refresh()
            $lastTitle = $client.MainWindowTitle
            $reconnected = $reconnected -or $client.MainWindowTitle -eq 'GammaRay · RDP 工作区'
            if ($client.MainWindowTitle -match '失败|已关闭|已断开|拒绝|超时|授权无效|已被占用') { $rejected = $true; break }
            if ($client.HasExited) { $rejected = $true; break }
            Start-Sleep -Milliseconds 200
        } while ([DateTime]::UtcNow -lt $deadline)
    }
} finally {
    if ($client) {
        if (-not $client.HasExited) {
            [void]$client.CloseMainWindow()
            if (-not $client.WaitForExit(5000)) { $client.Kill(); [void]$client.WaitForExit(5000) }
        }
        $normalExit = $client.HasExited -and $client.ExitCode -eq 0
        if ($stderr -and $stderr.Wait(3000)) {
            $codecErrors += [regex]::Matches($stderr.Result, 'decompress failed|decompress failure|DecodeFrame2 state|YUV.*failed|yuv.*failed').Count
            $connectionErrors = @([regex]::Matches($stderr.Result, 'ERRCONNECT_[A-Z_]+|ERRINFO_[A-Z_]+') |
                ForEach-Object Value | Select-Object -Unique)
            $rejected = $rejected -or $stderr.Result -match 'ERRCONNECT_(TLS_CONNECT_FAILED|AUTHENTICATION_FAILED)|certificate.*reject'
        }
        $client.Dispose()
    }
    try {
        if ($Scenario -in @('GraceExit', 'GraceReconnect') -and $instance -and $presented -and $normalExit) {
            # Observe natural runtime termination BEFORE any cleanup stop API.
            # A protocol-only Connect test must not hide a broken idle exit.
            # Five-second disconnect grace + bounded network shutdown + existing
            # Service/Console missing-process heartbeat reconciliation (15 s).
            $graceDeadline = [DateTime]::UtcNow.AddSeconds(45)
            do {
                Start-Sleep -Milliseconds 500
                try {
                    $instances = Invoke-ProbeApi '/api/v1/public/instances' $null $guest.access_token 3
                    $current = @($instances | Where-Object instance_id -eq $instance.instance_id)
                    if ($current.Count -eq 1) { $terminalState = [string]$current[0].state }
                    $graceExited = $current.Count -eq 1 -and $current[0].state -eq 'stopped'
                } catch { $graceExited = $false }
            } while (-not $graceExited -and $terminalState -ne 'failed' -and [DateTime]::UtcNow -lt $graceDeadline)
            if ($graceExited) { $instance.state = 'stopped' }
        }
        if ($instance -and $revoked) {
            # Check/stop only our exact instance AFTER observing revocation. Cleanup cannot prove the test passed.
            $login = Invoke-ProbeApi '/api/v1/session/admin/login' @{
                username = $cleanupCredential.UserName; password = $cleanupCredential.GetNetworkCredential().Password
            }
            $rows = Invoke-ProbeApi '/api/v1/app/control/app/instance/list' $null
            $own = @($rows | Where-Object instance_id -eq $instance.instance_id)
            if ($own.Count -eq 1 -and $own[0].state -in 'running', 'starting') {
                [void](Invoke-ProbeApi "/api/v1/app/control/app/instance/stop/$($instance.instance_id)" @{} '' 35 $login.csrf_token)
            }
            $cleanupCredential = $null
        } elseif ($instance -and $instance.state -eq 'running') {
            $rows = Invoke-ProbeApi '/api/v1/public/instances' $null $guest.access_token
            $own = @($rows | Where-Object instance_id -eq $instance.instance_id)
            if ($own.Count -eq 1 -and $own[0].state -in 'running', 'starting') {
                [void](Invoke-ProbeApi "/api/v1/public/instances/$($instance.instance_id)/stop" @{reason = 'bounded_rdp_acceptance_cleanup'} $guest.access_token)
            }
        }
    } finally { $owner.Dispose() }
}
$passed = if ($Scenario -eq 'RevokeSession') {
    $presented -and -not $rejected -and $revoked -and $revocationClosed -and $codecErrors -eq 0
} elseif ($Scenario -in @('Connect', 'GraceExit', 'GraceReconnect')) {
    $presented -and -not $rejected -and $normalExit -and $codecErrors -eq 0 -and
        ($Scenario -notin @('GraceExit', 'GraceReconnect') -or $graceExited) -and
        ($Scenario -ne 'GraceReconnect' -or $reconnected)
} else { -not $presented -and $rejected }
[pscustomobject]@{Scenario = $Scenario; InstanceId = $instance.instance_id; Passed = $passed; FirstFrameCallback = $presented; Rejected = $rejected;
    NormalExit = $normalExit; CodecErrorCount = $codecErrors; ConnectionErrors = $connectionErrors;
    GraceExited = $graceExited; GraceReconnected = $reconnected;
    TerminalState = $terminalState;
    RevocationClosed = $revocationClosed; RevocationSeconds = $revocationSeconds;
    LastWindowTitle = $lastTitle; Seconds = [int]([DateTime]::UtcNow - $startedAt).TotalSeconds}
if (-not $passed) { throw 'RDP acceptance did not pass; no visual/input/audio-listening claim is made by this protocol probe' }
