param(
    [Parameter(Mandatory = $true)]
    [string]$HostName,
    [int]$Port = 20369,
    [int]$TimeoutMilliseconds = 3000
)

$ErrorActionPreference = "Stop"

function Connect-WebSocket {
    param([string]$Path)

    $socket = New-Object System.Net.WebSockets.ClientWebSocket
    $cancel = New-Object System.Threading.CancellationTokenSource
    $cancel.CancelAfter($TimeoutMilliseconds)
    try {
        $uri = [Uri]("ws://{0}:{1}{2}" -f $HostName, $Port, $Path)
        [void]$socket.ConnectAsync($uri, $cancel.Token).GetAwaiter().GetResult()
        return $socket
    }
    catch {
        $socket.Dispose()
        throw
    }
    finally {
        $cancel.Dispose()
    }
}

function Wait-ForServerClose {
    param([System.Net.WebSockets.ClientWebSocket]$Socket)

    $buffer = New-Object byte[] 1
    $segment = New-Object System.ArraySegment[byte] -ArgumentList @(,$buffer)
    $cancel = New-Object System.Threading.CancellationTokenSource
    $cancel.CancelAfter($TimeoutMilliseconds)
    try {
        $result = $Socket.ReceiveAsync($segment, $cancel.Token).GetAwaiter().GetResult()
        return $result.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close
    }
    catch [System.OperationCanceledException] {
        return $false
    }
    catch [System.Net.WebSockets.WebSocketException] {
        return $true
    }
    finally {
        $cancel.Dispose()
    }
}

$publicSocket = $null
$rendererSocket = $null
$sysInfoSocket = $null
try {
    $publicSocket = Connect-WebSocket "/panel?stream_id=channel-policy-probe"
    if ($publicSocket.State -ne [System.Net.WebSockets.WebSocketState]::Open) {
        throw "The public /panel endpoint did not remain open."
    }

    $rendererSocket = Connect-WebSocket "/panel/renderer?instance_id=external-probe"
    if (-not (Wait-ForServerClose $rendererSocket)) {
        throw "The internal /panel/renderer endpoint accepted a non-loopback peer."
    }

    $sysInfoSocket = Connect-WebSocket "/sys/info"
    if (-not (Wait-ForServerClose $sysInfoSocket)) {
        throw "The internal /sys/info endpoint accepted a non-loopback peer."
    }

    [ordered]@{
        ok = $true
        host = $HostName
        port = $Port
        public_panel = "open"
        renderer_channel = "rejected"
        sys_info_channel = "rejected"
    } | ConvertTo-Json -Compress
}
finally {
    foreach ($socket in @($publicSocket, $rendererSocket, $sysInfoSocket)) {
        if ($null -ne $socket) {
            $socket.Dispose()
        }
    }
}
