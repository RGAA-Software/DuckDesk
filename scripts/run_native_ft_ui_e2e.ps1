param(
    [ValidateRange(1, 1000)]
    [int]$Rounds = 10,
    [ValidateRange(1, 1073741824)]
    [int64]$Bytes = 1048576,
    [ValidateRange(0, 10000)]
    [int]$SmallFiles = 0,
    [ValidateSet('Normal', 'Overwrite', 'Skip', 'Cancel')]
    [string]$TestMode = 'Normal',
    [string]$ConsoleBase = 'https://127.0.0.1:30500',
    [string]$TargetHost = '10.0.0.90',
    [ValidateRange(1, 65535)]
    [int]$TargetPort = 20371,
    [string]$DeviceId = '001190520',
    [string]$RemoteUser = 'Administrator',
    [ValidateRange(10, 900)]
    [int]$TransferTimeoutSeconds = 120,
    [string]$ClientExe = '',
    [string]$MongoExe = 'D:\software\mongodb_3.6\mongodb\bin\mongo.exe'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ClientExe) {
    $ClientExe = Join-Path $repoRoot 'build_official\dist\px_client.exe'
}
$ClientExe = (Resolve-Path -LiteralPath $ClientExe).Path
$dist = (Resolve-Path -LiteralPath (Join-Path $repoRoot 'build_official\dist')).Path
if (-not $ClientExe.StartsWith($dist, [StringComparison]::OrdinalIgnoreCase)) {
    throw "native acceptance must use build_official\dist: $ClientExe"
}

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PxNativeFtMouse {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(
        uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    public const uint LeftDown = 0x0002;
    public const uint LeftUp = 0x0004;
}
'@

function Invoke-JsonPost(
    [string]$Uri,
    [object]$Body,
    [string]$Bearer = '',
    [int]$Attempts = 9
) {
    $headers = @{}
    if ($Bearer) { $headers.Authorization = "Bearer $Bearer" }
    $request = @{
        Method = 'Post'
        Uri = $Uri
        Headers = $headers
        ContentType = 'application/json'
        Body = ($Body | ConvertTo-Json -Compress -Depth 12)
        TimeoutSec = 30
    }
    if ($Uri.StartsWith('https://') -and
        (Get-Command Invoke-RestMethod).Parameters.ContainsKey('SkipCertificateCheck')) {
        $request.SkipCertificateCheck = $true
        $request.SslProtocol = 'Tls12'
    }
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        try {
            return Invoke-RestMethod @request
        } catch {
            $status = [int]$_.Exception.Response.StatusCode
            if ($attempt -eq $Attempts -or ($status -ne 0 -and $status -ne 429)) {
                throw
            }
            Start-Sleep -Seconds $(if ($status -eq 429) { 10 } else { 1 })
        }
    }
}

function ConvertTo-Base64([string]$Value) {
    [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Value))
}

function New-TestFile([string]$Path, [int64]$Length) {
    $parent = Split-Path -Parent $Path
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    $stream = [IO.File]::Open(
        $Path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $random = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $buffer = New-Object byte[] (1024 * 1024)
        $remaining = $Length
        while ($remaining -gt 0) {
            $count = [int][Math]::Min($buffer.Length, $remaining)
            $random.GetBytes($buffer, 0, $count)
            $stream.Write($buffer, 0, $count)
            $remaining -= $count
        }
        $stream.Flush($true)
    } finally {
        $random.Dispose()
        $stream.Dispose()
    }
}

function New-SmallFileTree([string]$Root, [int]$Count) {
    [IO.Directory]::CreateDirectory($Root) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $Root 'empty directory 空目录')) | Out-Null
    for ($index = 0; $index -lt $Count; $index++) {
        $directory = Join-Path $Root ("folder_{0:D3}_测试" -f ($index % 100))
        [IO.Directory]::CreateDirectory($directory) | Out-Null
        $path = Join-Path $directory ("file_{0:D5} space.txt" -f $index)
        [IO.File]::WriteAllText(
            $path,
            "native-ft-ui-small-file index=$index path=$path",
            [Text.UTF8Encoding]::new($false))
    }
}

function Get-TreeManifest([string]$Root) {
    $aggregate = [Security.Cryptography.IncrementalHash]::CreateHash(
        [Security.Cryptography.HashAlgorithmName]::SHA256)
    try {
        $files = @(Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object {
            [IO.Path]::GetRelativePath($Root, $_.FullName)
        })
        $buffer = New-Object byte[] (64 * 1024)
        foreach ($file in $files) {
            $relative = [IO.Path]::GetRelativePath($Root, $file.FullName).Replace('\', '/')
            $metadata = [Text.Encoding]::UTF8.GetBytes("$relative`0$($file.Length)`0")
            $aggregate.AppendData($metadata)
            $stream = [IO.File]::Open(
                $file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                [IO.FileShare]::Read)
            try {
                while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $aggregate.AppendData($buffer, 0, $read)
                }
            } finally {
                $stream.Dispose()
            }
        }
        [Convert]::ToHexString($aggregate.GetHashAndReset())
    } finally {
        $aggregate.Dispose()
    }
}

function Get-ClientRoot([Diagnostics.Process]$Process) {
    $Process.Refresh()
    if ($Process.HasExited -or $Process.MainWindowHandle -eq 0) { return $null }
    [Windows.Automation.AutomationElement]::FromHandle([IntPtr]$Process.MainWindowHandle)
}

function Find-ByName(
    [Windows.Automation.AutomationElement]$Root,
    [string[]]$Names,
    [ValidateSet('any', 'local', 'remote')]
    [string]$Side = 'any'
) {
    $bounds = $Root.Current.BoundingRectangle
    $middle = $bounds.Left + $bounds.Width / 2
    foreach ($name in $Names) {
        $condition = [Windows.Automation.PropertyCondition]::new(
            [Windows.Automation.AutomationElement]::NameProperty, $name)
        $matches = $Root.FindAll(
            [Windows.Automation.TreeScope]::Descendants, $condition)
        for ($index = 0; $index -lt $matches.Count; $index++) {
            $element = $matches.Item($index)
            $rect = $element.Current.BoundingRectangle
            $center = $rect.Left + $rect.Width / 2
            if ($Side -eq 'any' -or
                ($Side -eq 'local' -and $center -lt $middle) -or
                ($Side -eq 'remote' -and $center -gt $middle)) {
                return $element
            }
        }
    }
    $null
}

function Wait-ByName(
    [Windows.Automation.AutomationElement]$Root,
    [string[]]$Names,
    [ValidateSet('any', 'local', 'remote')]
    [string]$Side,
    [int]$TimeoutSeconds = 20
) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $element = Find-ByName $Root $Names $Side
        if ($null -ne $element) { return $element }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw "UI item not found: names=$($Names -join '|') side=$Side"
}

function Wait-ClientByName(
    [Diagnostics.Process]$Process,
    [string[]]$Names,
    [ValidateSet('any', 'local', 'remote')]
    [string]$Side,
    [int]$TimeoutSeconds = 30
) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $root = Get-ClientRoot $Process
        if ($null -ne $root) {
            $element = Find-ByName $root $Names $Side
            if ($null -ne $element) {
                return [pscustomobject]@{ Root = $root; Element = $element }
            }
        }
        Start-Sleep -Milliseconds 250
    } while (-not $Process.HasExited -and (Get-Date) -lt $deadline)
    throw "client UI item not found: names=$($Names -join '|') side=$Side title=$($Process.MainWindowTitle)"
}

function Find-ProcessElementByName(
    [Diagnostics.Process]$Process,
    [string[]]$Names
) {
    $desktop = [Windows.Automation.AutomationElement]::RootElement
    $processCondition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::ProcessIdProperty, $Process.Id)
    $windows = $desktop.FindAll(
        [Windows.Automation.TreeScope]::Children, $processCondition)
    for ($windowIndex = 0; $windowIndex -lt $windows.Count; $windowIndex++) {
        $window = $windows.Item($windowIndex)
        foreach ($name in $Names) {
            $nameCondition = [Windows.Automation.PropertyCondition]::new(
                [Windows.Automation.AutomationElement]::NameProperty, $name)
            $matches = $window.FindAll(
                [Windows.Automation.TreeScope]::Descendants, $nameCondition)
            if ($matches.Count -gt 0) {
                return [pscustomobject]@{
                    Root = $window
                    Element = $matches.Item(0)
                }
            }
        }
    }
    $null
}

function Wait-ProcessElementByName(
    [Diagnostics.Process]$Process,
    [string[]]$Names,
    [int]$TimeoutSeconds = 20
) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $match = Find-ProcessElementByName $Process $Names
        if ($null -ne $match) { return $match }
        Start-Sleep -Milliseconds 100
    } while (-not $Process.HasExited -and (Get-Date) -lt $deadline)
    throw "process UI item not found: names=$($Names -join '|') title=$($Process.MainWindowTitle)"
}

function Click-Element(
    [Windows.Automation.AutomationElement]$Element,
    [switch]$Double
) {
    $rect = $Element.Current.BoundingRectangle
    if ($rect.Width -le 0 -or $rect.Height -le 0) {
        throw 'UI element has no clickable rectangle'
    }
    $x = [int]($rect.Left + $rect.Width / 2)
    $y = [int]($rect.Top + $rect.Height / 2)
    [void][PxNativeFtMouse]::SetCursorPos($x, $y)
    foreach ($click in 1..$(if ($Double) { 2 } else { 1 })) {
        [PxNativeFtMouse]::mouse_event(
            [PxNativeFtMouse]::LeftDown, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 80
        [PxNativeFtMouse]::mouse_event(
            [PxNativeFtMouse]::LeftUp, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 100
    }
    Start-Sleep -Milliseconds 250
}

function Get-ToolbarButtons(
    [Windows.Automation.AutomationElement]$Root,
    [ValidateSet('local', 'remote')]
    [string]$Side
) {
    $bounds = $Root.Current.BoundingRectangle
    $middle = $bounds.Left + $bounds.Width / 2
    $all = $Root.FindAll(
        [Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.Condition]::TrueCondition)
    $buttons = @()
    for ($index = 0; $index -lt $all.Count; $index++) {
        $element = $all.Item($index)
        $rect = $element.Current.BoundingRectangle
        $center = $rect.Left + $rect.Width / 2
        if ($element.Current.ControlType -eq [Windows.Automation.ControlType]::Button -and
            [string]::IsNullOrEmpty($element.Current.Name) -and
            $rect.Top -lt ($bounds.Top + 150) -and
            (($Side -eq 'local' -and $center -lt $middle) -or
             ($Side -eq 'remote' -and $center -gt $middle))) {
            $buttons += $element
        }
    }
    @($buttons | Sort-Object { $_.Current.BoundingRectangle.Left })
}

function Get-UnnamedButtons(
    [Windows.Automation.AutomationElement]$Root
) {
    $all = $Root.FindAll(
        [Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.Condition]::TrueCondition)
    $buttons = @()
    for ($index = 0; $index -lt $all.Count; $index++) {
        $element = $all.Item($index)
        if ($element.Current.ControlType -eq [Windows.Automation.ControlType]::Button -and
            [string]::IsNullOrEmpty($element.Current.Name)) {
            $buttons += $element
        }
    }
    $buttons
}

function Invoke-TransferCancel(
    [Diagnostics.Process]$Process,
    [string]$JobName,
    [int]$TimeoutSeconds = 10
) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $expanded = $false
    $lastButtons = @()
    do {
        $root = Get-ClientRoot $Process
        if ($null -eq $root) {
            Start-Sleep -Milliseconds 50
            continue
        }
        $bounds = $root.Current.BoundingRectangle
        $all = $root.FindAll(
            [Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.Condition]::TrueCondition)
        $lastButtons = @()
        for ($index = 0; $index -lt $all.Count; $index++) {
            $candidate = $all.Item($index)
            if ($candidate.Current.ControlType -eq [Windows.Automation.ControlType]::Button) {
                $candidateRect = $candidate.Current.BoundingRectangle
                $lastButtons += "name='$($candidate.Current.Name)' x=$([int]$candidateRect.Left) y=$([int]$candidateRect.Top) w=$([int]$candidateRect.Width) h=$([int]$candidateRect.Height) enabled=$($candidate.Current.IsEnabled)"
            }
        }
        $buttons = @(Get-UnnamedButtons $root)
        if (-not $expanded) {
            $expand = @($buttons | Where-Object {
                $rect = $_.Current.BoundingRectangle
                $_.Current.IsEnabled -and
                $rect.Width -ge 28 -and $rect.Width -le 36 -and
                $rect.Height -ge 24 -and $rect.Height -le 32 -and
                $rect.Top -gt ($bounds.Top + 150)
            } | Sort-Object { $_.Current.BoundingRectangle.Top } -Descending | Select-Object -First 1)
            if ($expand.Count -gt 0) {
                Click-Element $expand[0]
                $expanded = $true
                continue
            }
        } else {
            $cancel = @($buttons | Where-Object {
                $rect = $_.Current.BoundingRectangle
                $_.Current.IsEnabled -and
                $rect.Width -ge 18 -and $rect.Width -le 24 -and
                $rect.Height -ge 18 -and $rect.Height -le 24 -and
                $rect.Top -gt ($bounds.Top + 150)
            } | Sort-Object { $_.Current.BoundingRectangle.Top } | Select-Object -First 1)
            if ($cancel.Count -gt 0) {
                Click-Element $cancel[0]
                return
            }
            # Qt does not expose QTableWidget cell widgets through UI Automation on
            # every Windows build. Derive the stable cancel-column X coordinate and
            # the exact row Y coordinate from the accessible file-name cell.
            $row = Find-ByName $root @($JobName) 'any'
            if ($null -ne $row) {
                $rowRect = $row.Current.BoundingRectangle
                $x = [int]($bounds.Right - 41)
                $y = [int]($rowRect.Top + $rowRect.Height / 2)
                [void][PxNativeFtMouse]::SetForegroundWindow(
                    [IntPtr]$Process.MainWindowHandle)
                [void][PxNativeFtMouse]::SetCursorPos($x, $y)
                [PxNativeFtMouse]::mouse_event(
                    [PxNativeFtMouse]::LeftDown, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 80
                [PxNativeFtMouse]::mouse_event(
                    [PxNativeFtMouse]::LeftUp, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 250
                return
            }
        }
        Start-Sleep -Milliseconds 50
    } while (-not $Process.HasExited -and (Get-Date) -lt $deadline)
    throw "transfer queue cancel button was not found; expanded=$expanded buttons=$($lastButtons -join '; ')"
}

function Close-TransferQueue(
    [Diagnostics.Process]$Process
) {
    $root = Get-ClientRoot $Process
    if ($null -eq $root) { throw 'native FT window closed while collapsing queue' }
    $buttons = @(Get-UnnamedButtons $root | Where-Object {
        $rect = $_.Current.BoundingRectangle
        $_.Current.IsEnabled -and
        $rect.Width -ge 28 -and $rect.Width -le 36 -and
        $rect.Height -ge 24 -and $rect.Height -le 32
    } | Sort-Object { $_.Current.BoundingRectangle.Top } -Descending)
    if ($buttons.Count -eq 0) { throw 'transfer queue collapse button was not found' }
    Click-Element $buttons[0]
    (Wait-ClientByName $Process @('Desktop', '桌面') 'local' 10).Root
}

function Wait-FileComplete([string]$Path, [int64]$Length, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (Test-Path -LiteralPath $Path) {
            $item = Get-Item -LiteralPath $Path
            if ($item.Length -eq $Length) {
                Start-Sleep -Milliseconds 300
                if ((Get-Item -LiteralPath $Path).Length -eq $Length) { return }
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw "transfer timeout: $Path"
}

function Wait-FileUnlocked([string]$Path, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        try {
            $stream = [IO.File]::Open(
                $Path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite,
                [IO.FileShare]::None)
            $stream.Dispose()
            return
        } catch [IO.IOException] {
            Start-Sleep -Milliseconds 100
        }
    } while ((Get-Date) -lt $deadline)
    throw "file remained locked after cancel: $Path"
}

function Remove-TestArtifact([string]$Path, [int]$TimeoutSeconds = 10) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (-not (Test-Path -LiteralPath $Path)) { return }
        try {
            Remove-Item -LiteralPath $Path -Force -Recurse -ErrorAction Stop
            return
        } catch [IO.IOException] {
            Start-Sleep -Milliseconds 200
        } catch [UnauthorizedAccessException] {
            Start-Sleep -Milliseconds 200
        }
    } while ((Get-Date) -lt $deadline)
    throw "test artifact cleanup timed out: $Path"
}

function Wait-DirectoryComplete([string]$Path, [int]$Files, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (Test-Path -LiteralPath $Path) {
            $count = @(Get-ChildItem -LiteralPath $Path -File -Recurse -ErrorAction SilentlyContinue).Count
            if ($count -eq $Files) {
                Start-Sleep -Seconds 1
                $count = @(Get-ChildItem -LiteralPath $Path -File -Recurse -ErrorAction SilentlyContinue).Count
                if ($count -eq $Files) { return }
            }
        }
        Start-Sleep -Seconds 2
    } while ((Get-Date) -lt $deadline)
    throw "directory transfer timeout: path=$Path expected_files=$Files"
}

function Remove-TestAccount([string]$Uid) {
    if (-not $Uid -or $Uid -notmatch '^[A-Za-z0-9_-]+$' -or
        -not (Test-Path -LiteralPath $MongoExe)) {
        return
    }
    $cleanup = @"
var u='$Uid';
var registration=db.c_event.findOne({action:'user_register',target_id:u,result:'success'});
if(registration){db.c_user_session.deleteMany({subject_id:registration.actor_id});}
db.c_connection_ticket.deleteMany({subject_id:u});
db.c_user_session.deleteMany({subject_id:u});
db.c_user_group_member.deleteMany({uid:u});
db.c_user_device.deleteMany({uid:u});
db.c_user.deleteMany({uid:u});
db.c_event.deleteMany({`$or:[{actor_id:u},{target_id:u}]});
printjson({users:db.c_user.count({uid:u}),sessions:db.c_user_session.count({subject_id:u}),tickets:db.c_connection_ticket.count({subject_id:u})});
"@
    & $MongoExe db_gr_console_server --quiet --eval $cleanup
}

$suffix = [guid]::NewGuid().ToString('N').Substring(0, 10)
$username = "ftui_$suffix"
$password = "T!$([guid]::NewGuid().ToString('N'))"
$nonce = "ftui_$suffix"
$uid = $null
$process = $null
$localDesktop = [Environment]::GetFolderPath('Desktop')
$remoteDesktop = "\\$TargetHost\C$\Users\$RemoteUser\Desktop"
$createdNames = [Collections.Generic.List[string]]::new()
$pass = 0

try {
    if ($SmallFiles -gt 0 -and $TestMode -ne 'Normal') {
        throw 'SmallFiles is supported only in Normal mode'
    }
    if (-not (Test-Path -LiteralPath $remoteDesktop)) {
        throw "remote acceptance directory is unavailable: $remoteDesktop"
    }
    $guest = Invoke-JsonPost "$ConsoleBase/api/v1/session/guest" `
        @{ client_nonce = "ftui_guest_$suffix"; client_type = 'panel' }
    $registered = Invoke-JsonPost "$ConsoleBase/api/v1/user/register" `
        @{ username = $username; password = $password } $guest.data.access_token
    $uid = $registered.data.uid
    $login = Invoke-JsonPost "$ConsoleBase/api/v1/session/user/login" `
        @{ username = $username; password = $password; client_type = 'panel' }
    $issued = Invoke-JsonPost "$ConsoleBase/api/v1/user/devices/$DeviceId/ticket" `
        @{ client_nonce = $nonce; requested_permissions = @('view', 'file') } `
        $login.data.access_token
    if (-not $issued.data.ticket) { throw 'Console returned an empty FT ticket' }

    $arguments = @(
        "--host=$TargetHost", "--port=$TargetPort",
        "--console_host=$(([uri]$ConsoleBase).Host)",
        "--console_port=$(([uri]$ConsoleBase).Port)",
        "--stream_id=ftui_$suffix", '--conn_type=console_ticket', '--network_type=ws',
        "--device_id=ftui_client_$suffix", "--remote_device_id=$DeviceId",
        "--stream_name=$(ConvertTo-Base64 "FT-$DeviceId")",
        "--connection_ticket=$(ConvertTo-Base64 ([string]$issued.data.ticket))",
        "--connection_nonce=$nonce", '--mode=file-transfer', '--enable_p2p=0',
        '--only_viewing=1'
    )
    $process = Start-Process -FilePath $ClientExe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $ClientExe -Parent) -PassThru
    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        $root = if ($process.MainWindowTitle -like 'File Transfer*') {
            Get-ClientRoot $process
        } else {
            $null
        }
    } while ($null -eq $root -and (Get-Date) -lt $deadline)
    if ($null -eq $root) { throw 'native FT window did not open' }
    [void][PxNativeFtMouse]::SetForegroundWindow([IntPtr]$process.MainWindowHandle)

    $localReady = Wait-ClientByName $process @('Desktop', '桌面') 'local'
    $root = $localReady.Root
    Click-Element $localReady.Element -Double
    $remoteHome = Wait-ByName $root @($RemoteUser) 'remote'
    Click-Element $remoteHome -Double
    $remoteDesktopItem = Wait-ByName $root @('Desktop', '桌面') 'remote'
    Click-Element $remoteDesktopItem -Double

    for ($round = 1; $round -le $Rounds; $round++) {
        if ($process.HasExited) { throw "native FT client exited before round $round" }
        $name = if ($SmallFiles -gt 0) {
            "px_ft_ui_dir_${suffix}_${round}"
        } else {
            "px_ft_ui_${suffix}_${round}.bin"
        }
        $createdNames.Add($name)
        $localPath = Join-Path $localDesktop $name
        $remotePath = Join-Path $remoteDesktop $name
        if ($SmallFiles -gt 0) {
            New-SmallFileTree $localPath $SmallFiles
            $sourceHash = Get-TreeManifest $localPath
        } else {
            New-TestFile $localPath $Bytes
            $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $localPath).Hash
        }

        $preexistingHash = ''
        if ($TestMode -in @('Overwrite', 'Skip')) {
            New-TestFile $remotePath ([Math]::Max(1, [Math]::Min($Bytes, 4096)))
            $preexistingHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $remotePath).Hash
            if ($preexistingHash -eq $sourceHash) {
                throw "pre-existing conflict fixture unexpectedly matches source in round $round"
            }
        }

        $localButtons = Get-ToolbarButtons $root 'local'
        $remoteButtons = Get-ToolbarButtons $root 'remote'
        if ($localButtons.Count -lt 4 -or $remoteButtons.Count -lt 4) {
            throw 'native FT toolbar is incomplete'
        }
        if ($TestMode -in @('Overwrite', 'Skip')) {
            Click-Element $remoteButtons[2]
            [void](Wait-ByName $root @($name) 'remote')
        }
        Click-Element $localButtons[1]
        $localRow = Wait-ByName $root @($name) 'local'
        Click-Element $localRow
        Click-Element $localButtons[-1]

        if ($TestMode -in @('Overwrite', 'Skip')) {
            $decisionNames = if ($TestMode -eq 'Overwrite') {
                @('Overwrite', '覆盖', '覆蓋')
            } else {
                @('Skip', '跳过', '跳過')
            }
            $decision = Wait-ProcessElementByName $process $decisionNames 20
            [void][PxNativeFtMouse]::SetForegroundWindow(
                [IntPtr]$decision.Root.Current.NativeWindowHandle)
            Click-Element $decision.Element
        }

        if ($TestMode -eq 'Cancel') {
            Invoke-TransferCancel $process $name 10
            Start-Sleep -Seconds 2
            if ($process.HasExited) { throw "native FT client exited after cancel in round $round" }
            Wait-FileUnlocked $localPath 30
            $root = Close-TransferQueue $process
            if (Test-Path -LiteralPath $remotePath) {
                $cancelledLength = (Get-Item -LiteralPath $remotePath).Length
                if ($cancelledLength -eq $Bytes) {
                    $cancelledHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $remotePath).Hash
                    if ($cancelledHash -eq $sourceHash) {
                        throw "cancel completed the full upload in round $round"
                    }
                }
            }
            $partialPath = "$remotePath.download"
            if (Test-Path -LiteralPath $partialPath) {
                Remove-Item -LiteralPath $partialPath -Force -ErrorAction SilentlyContinue
            }
            Remove-Item -LiteralPath $localPath -Force
            if (Test-Path -LiteralPath $remotePath) {
                Remove-Item -LiteralPath $remotePath -Force -ErrorAction SilentlyContinue
            }
            $pass++
            Write-Host ("NATIVE_FT_UI_ROUND {0:D3}/{1} PASS mode=Cancel bytes={2}" -f `
                $round, $Rounds, $Bytes)
            continue
        }

        if ($SmallFiles -gt 0) {
            Wait-DirectoryComplete $remotePath $SmallFiles $TransferTimeoutSeconds
            $uploadHash = Get-TreeManifest $remotePath
        } else {
            $expectedLength = if ($TestMode -eq 'Skip') {
                [Math]::Max(1, [Math]::Min($Bytes, 4096))
            } else {
                $Bytes
            }
            Wait-FileComplete $remotePath $expectedLength $TransferTimeoutSeconds
            $uploadHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $remotePath).Hash
        }
        $expectedHash = if ($TestMode -eq 'Skip') { $preexistingHash } else { $sourceHash }
        if ($uploadHash -ne $expectedHash) {
            throw "upload SHA-256 mismatch in round $round mode=$TestMode"
        }

        if ($TestMode -eq 'Skip') {
            Click-Element $remoteButtons[2]
        }
        $remoteRow = Wait-ByName $root @($name) 'remote' $TransferTimeoutSeconds
        Remove-Item -LiteralPath $localPath -Force -Recurse
        Click-Element $remoteRow
        Click-Element $remoteButtons[0]
        if ($SmallFiles -gt 0) {
            Wait-DirectoryComplete $localPath $SmallFiles $TransferTimeoutSeconds
            $downloadHash = Get-TreeManifest $localPath
        } else {
            Wait-FileComplete $localPath $expectedLength $TransferTimeoutSeconds
            $downloadHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $localPath).Hash
        }
        if ($downloadHash -ne $expectedHash) {
            throw "download SHA-256 mismatch in round $round mode=$TestMode"
        }

        Remove-Item -LiteralPath $localPath -Force -Recurse
        Remove-Item -LiteralPath $remotePath -Force -Recurse
        $pass++
        Write-Host ("NATIVE_FT_UI_ROUND {0:D3}/{1} PASS mode={2} bytes={3} files={4} hash={5}" -f `
            $round, $Rounds, $TestMode, $(if ($SmallFiles -gt 0) { 0 } else { $Bytes }),
            $SmallFiles, $expectedHash)
    }
} finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        [void]$process.WaitForExit(5000)
    }
    foreach ($name in $createdNames) {
        $localPath = Join-Path $localDesktop $name
        $remotePath = Join-Path $remoteDesktop $name
        if (Test-Path -LiteralPath $localPath) {
            Remove-TestArtifact $localPath
        }
        if (Test-Path -LiteralPath $remotePath) {
            Remove-TestArtifact $remotePath
        }
        foreach ($suffixToRemove in @('.download', '.digest')) {
            $sidecarPath = "$remotePath$suffixToRemove"
            if (Test-Path -LiteralPath $sidecarPath) {
                Remove-TestArtifact $sidecarPath
            }
        }
    }
    Remove-TestAccount $uid
}

Write-Host "NATIVE_FT_UI_SUMMARY pass=$pass fail=$($Rounds - $pass)"
if ($pass -ne $Rounds) { exit 1 }
