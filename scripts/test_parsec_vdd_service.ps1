param(
    [ValidateRange(1, 100)]
    [int]$Rounds = 10,

    [ValidateRange(1, 8)]
    [int]$Capacity = 8,

    [ValidateRange(0, 120)]
    [int]$HoldAfterCreateSeconds = 0,

    [switch]$SkipCapacityGate,

    [string]$ServiceUri = "ws://127.0.0.1:20375/service/message?from=panel",

    [string]$OutputPath = "C:\Windows\Temp\PixelsAcceptance\parsec_vdd_service_acceptance.json"
)

$ErrorActionPreference = "Stop"

function Add-Varint {
    param(
        [System.Collections.Generic.List[byte]]$Buffer,
        [UInt64]$Value
    )
    do {
        $next = [byte]($Value -band 0x7f)
        $Value = $Value -shr 7
        if ($Value -ne 0) { $next = $next -bor 0x80 }
        $Buffer.Add($next)
    } while ($Value -ne 0)
}

function Add-BytesField {
    param(
        [System.Collections.Generic.List[byte]]$Buffer,
        [int]$FieldNumber,
        [byte[]]$Value
    )
    Add-Varint $Buffer ([UInt64](($FieldNumber -shl 3) -bor 2))
    Add-Varint $Buffer ([UInt64]$Value.Length)
    $Buffer.AddRange($Value)
}

function Add-VarintField {
    param(
        [System.Collections.Generic.List[byte]]$Buffer,
        [int]$FieldNumber,
        [UInt64]$Value
    )
    Add-Varint $Buffer ([UInt64]($FieldNumber -shl 3))
    Add-Varint $Buffer $Value
}

function New-VirtualDisplayRequest {
    param(
        [string]$RequestId,
        [ValidateSet("create", "remove", "query", "reset")]
        [string]$Operation,
        [UInt32]$Width = 1920,
        [UInt32]$Height = 1080,
        [UInt32]$RefreshHz = 60
    )
    $operationValue = @{ create = 0; remove = 1; query = 2; reset = 3 }[$Operation]
    $request = [System.Collections.Generic.List[byte]]::new()
    Add-BytesField $request 1 ([Text.Encoding]::UTF8.GetBytes($RequestId))
    Add-VarintField $request 2 ([UInt64]$operationValue)
    Add-VarintField $request 3 $Width
    Add-VarintField $request 4 $Height
    Add-VarintField $request 5 $RefreshHz

    $message = [System.Collections.Generic.List[byte]]::new()
    Add-VarintField $message 1 9 # kSrvVirtualDisplayRequest
    Add-BytesField $message 11 $request.ToArray()
    return $message.ToArray()
}

function Read-Varint {
    param([byte[]]$Bytes, [ref]$Offset)
    [UInt64]$value = 0
    $shift = 0
    do {
        if ($Offset.Value -ge $Bytes.Length -or $shift -ge 70) {
            throw "Invalid protobuf varint"
        }
        $current = $Bytes[$Offset.Value]
        $Offset.Value++
        $value = $value -bor ([UInt64]($current -band 0x7f) -shl $shift)
        $shift += 7
    } while (($current -band 0x80) -ne 0)
    return $value
}

function Read-LengthDelimited {
    param([byte[]]$Bytes, [ref]$Offset)
    $length = [int](Read-Varint $Bytes $Offset)
    if ($length -lt 0 -or $Offset.Value + $length -gt $Bytes.Length) {
        throw "Invalid protobuf length-delimited field"
    }
    $value = [byte[]]::new($length)
    [Array]::Copy($Bytes, $Offset.Value, $value, 0, $length)
    $Offset.Value += $length
    return $value
}

function Skip-ProtobufField {
    param([byte[]]$Bytes, [ref]$Offset, [int]$WireType)
    switch ($WireType) {
        0 { $null = Read-Varint $Bytes $Offset }
        1 { $Offset.Value += 8 }
        2 { $null = Read-LengthDelimited $Bytes $Offset }
        5 { $Offset.Value += 4 }
        default { throw "Unsupported protobuf wire type $WireType" }
    }
    if ($Offset.Value -gt $Bytes.Length) { throw "Protobuf field exceeds payload" }
}

function ConvertFrom-VirtualDisplayResult {
    param([byte[]]$MessageBytes)
    $offset = 0
    $messageType = $null
    $resultBytes = $null
    while ($offset -lt $MessageBytes.Length) {
        $tag = Read-Varint $MessageBytes ([ref]$offset)
        $field = [int]($tag -shr 3)
        $wire = [int]($tag -band 7)
        if ($field -eq 1 -and $wire -eq 0) {
            $messageType = [int](Read-Varint $MessageBytes ([ref]$offset)
            )
        } elseif ($field -eq 12 -and $wire -eq 2) {
            $resultBytes = Read-LengthDelimited $MessageBytes ([ref]$offset)
        } else {
            Skip-ProtobufField $MessageBytes ([ref]$offset) $wire
        }
    }
    if ($messageType -ne 10 -or $null -eq $resultBytes) {
        throw "Expected kSrvVirtualDisplayResult, got message type $messageType"
    }

    $result = [ordered]@{
        request_id = ""; accepted = $false; topology_changed = $false
        topology_generation = [UInt64]0; logical_display_id = ""
        error_code = ""; error_message = ""; owned_display_count = 0
        actual_virtual_display_count = 0; driver_installed = $false
        package_valid = $false; removal_safe = $false; phase = ""
    }
    $offset = 0
    while ($offset -lt $resultBytes.Length) {
        $tag = Read-Varint $resultBytes ([ref]$offset)
        $field = [int]($tag -shr 3)
        $wire = [int]($tag -band 7)
        if ($wire -eq 2 -and $field -in 1, 5, 6, 7, 13) {
            $text = [Text.Encoding]::UTF8.GetString((Read-LengthDelimited $resultBytes ([ref]$offset)))
            switch ($field) {
                1 { $result.request_id = $text }
                5 { $result.logical_display_id = $text }
                6 { $result.error_code = $text }
                7 { $result.error_message = $text }
                13 { $result.phase = $text }
            }
        } elseif ($wire -eq 0 -and $field -in 2, 3, 4, 8, 9, 10, 11, 12) {
            $value = Read-Varint $resultBytes ([ref]$offset)
            switch ($field) {
                2 { $result.accepted = $value -ne 0 }
                3 { $result.topology_changed = $value -ne 0 }
                4 { $result.topology_generation = $value }
                8 { $result.owned_display_count = [int]$value }
                9 { $result.actual_virtual_display_count = [int]$value }
                10 { $result.driver_installed = $value -ne 0 }
                11 { $result.package_valid = $value -ne 0 }
                12 { $result.removal_safe = $value -ne 0 }
            }
        } else {
            Skip-ProtobufField $resultBytes ([ref]$offset) $wire
        }
    }
    return [pscustomobject]$result
}

function Receive-BinaryMessage {
    param([System.Net.WebSockets.ClientWebSocket]$Socket)
    $bytes = [System.Collections.Generic.List[byte]]::new()
    do {
        $buffer = [byte[]]::new(8192)
        $segment = [ArraySegment[byte]]::new($buffer)
        $receive = $Socket.ReceiveAsync($segment, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        if ($receive.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) {
            throw "Service websocket closed while waiting for a result"
        }
        if ($receive.Count -gt 0) {
            $chunk = [byte[]]$buffer[0..($receive.Count - 1)]
            $bytes.AddRange($chunk)
        }
    } while (-not $receive.EndOfMessage)
    return $bytes.ToArray()
}

function Invoke-VirtualDisplayOperation {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$Operation,
        [string]$RequestId
    )
    $payload = New-VirtualDisplayRequest -RequestId $RequestId -Operation $Operation
    $segment = [ArraySegment[byte]]::new($payload)
    $null = $Socket.SendAsync(
        $segment,
        [Net.WebSockets.WebSocketMessageType]::Binary,
        $true,
        [Threading.CancellationToken]::None
    ).GetAwaiter().GetResult()
    $result = ConvertFrom-VirtualDisplayResult (Receive-BinaryMessage $Socket)
    if ($result.request_id -ne $RequestId) {
        throw "Mismatched response id '$($result.request_id)', expected '$RequestId'"
    }
    return $result
}

function Assert-Result {
    param($Result, [bool]$Accepted, [int]$Owned, [int]$Actual, [string]$Context)
    if ($Result.accepted -ne $Accepted -or
        $Result.owned_display_count -ne $Owned -or
        $Result.actual_virtual_display_count -ne $Actual) {
        throw "$Context failed: $($Result | ConvertTo-Json -Compress)"
    }
}

$startedAt = Get-Date
$socket = [System.Net.WebSockets.ClientWebSocket]::new()
$records = [System.Collections.Generic.List[object]]::new()
try {
    $null = $socket.ConnectAsync([Uri]$ServiceUri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $query = Invoke-VirtualDisplayOperation $socket query "accept-query-$([Guid]::NewGuid().ToString('N'))"
    Assert-Result $query $true 0 0 "initial query"

    for ($round = 1; $round -le $Rounds; $round++) {
        $roundStarted = Get-Date
        $create = Invoke-VirtualDisplayOperation $socket create "accept-create-$round-$([Guid]::NewGuid().ToString('N'))"
        Assert-Result $create $true 1 1 "round $round create"
        $controller = @(Get-Process -Name px_display -ErrorAction SilentlyContinue)
        $mode = Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
            Where-Object Name -eq "Parsec Virtual Display Adapter" |
            Select-Object -First 1
        if ($controller.Count -ne 1 -or $controller[0].SessionId -eq 0) {
            throw "round $round controller is not unique in an interactive session"
        }
        if ($mode.CurrentHorizontalResolution -ne 1920 -or
            $mode.CurrentVerticalResolution -ne 1080 -or
            $mode.CurrentRefreshRate -ne 60) {
            throw "round $round mode is not 1920x1080@60"
        }
        if ($HoldAfterCreateSeconds -gt 0) {
            Start-Sleep -Seconds $HoldAfterCreateSeconds
        }

        $remove = Invoke-VirtualDisplayOperation $socket remove "accept-remove-$round-$([Guid]::NewGuid().ToString('N'))"
        Assert-Result $remove $true 0 0 "round $round remove"
        $records.Add([pscustomobject]@{
            round = $round
            create_generation = $create.topology_generation
            remove_generation = $remove.topology_generation
            elapsed_ms = [int]((Get-Date) - $roundStarted).TotalMilliseconds
        })
    }

    $capacityRecords = [System.Collections.Generic.List[object]]::new()
    $overflow = $null
    if (-not $SkipCapacityGate) {
        for ($index = 1; $index -le $Capacity; $index++) {
            $create = Invoke-VirtualDisplayOperation $socket create "capacity-create-$index-$([Guid]::NewGuid().ToString('N'))"
            Assert-Result $create $true $index $index "capacity create $index"
            $capacityRecords.Add($create)
        }
        $overflow = Invoke-VirtualDisplayOperation $socket create "capacity-overflow-$([Guid]::NewGuid().ToString('N'))"
        if ($overflow.accepted -or $overflow.error_code -ne "VIRTUAL_DISPLAY_LIMIT_REACHED") {
            throw "capacity overflow did not return VIRTUAL_DISPLAY_LIMIT_REACHED: $($overflow | ConvertTo-Json -Compress)"
        }
        for ($remaining = $Capacity - 1; $remaining -ge 0; $remaining--) {
            $remove = Invoke-VirtualDisplayOperation $socket remove "capacity-remove-$remaining-$([Guid]::NewGuid().ToString('N'))"
            Assert-Result $remove $true $remaining $remaining "capacity remove to $remaining"
        }
    }

    $processes = Get-Process px_service, px_panel, px_render, px_display -ErrorAction SilentlyContinue |
        Select-Object ProcessName, Id, SessionId, Responding
    $result = [ordered]@{
        passed = $true
        started_at = $startedAt.ToString("o")
        finished_at = (Get-Date).ToString("o")
        rounds = $Rounds
        capacity = $Capacity
        records = $records
        capacity_generations = if ($capacityRecords.Count -gt 0) {
            @($capacityRecords.topology_generation)
        } else {
            @()
        }
        overflow_error = if ($overflow) { $overflow.error_code } else { $null }
        final_owned_count = $remove.owned_display_count
        final_actual_count = $remove.actual_virtual_display_count
        processes = $processes
    }
} catch {
    $result = [ordered]@{
        passed = $false
        started_at = $startedAt.ToString("o")
        finished_at = (Get-Date).ToString("o")
        error = $_.Exception.ToString()
        records = $records
    }
} finally {
    if ($socket.State -eq [Net.WebSockets.WebSocketState]::Open) {
        $null = $socket.CloseAsync(
            [Net.WebSockets.WebSocketCloseStatus]::NormalClosure,
            "acceptance complete",
            [Threading.CancellationToken]::None
        ).GetAwaiter().GetResult()
    }
    $socket.Dispose()
}

$parent = Split-Path -Parent $OutputPath
if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($result.passed) { exit 0 }
exit 1
