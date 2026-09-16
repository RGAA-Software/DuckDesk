param(
    [Parameter(Mandatory = $true)]
    [Alias("ResultPath")]
    [string]$OutputPath,

    [ValidateSet("cloud_node", "client", "remote")]
    [string]$Product = "cloud_node",

    [string]$InstallRoot = "",

    # Optional scheduled-task parameters let this read-only audit run on
    # locked-down test hosts.
    [string]$Mode,
    [string]$Executable
)

$ErrorActionPreference = "Stop"

$productInfo = @{
    cloud_node = @{ directory = "C:\Program Files\Pixels Cloud Node"; uninstall_key = "PixelsCloudNode" }
    client = @{ directory = "C:\Program Files\Pixels Client"; uninstall_key = "PixelsClient" }
    remote = @{ directory = "C:\Program Files\Pixels Remote"; uninstall_key = "PixelsRemote" }
}[$Product]
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = $productInfo.directory
}

$uninstallKeys = @(
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$($productInfo.uninstall_key)",
    "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\$($productInfo.uninstall_key)"
)
$service = Get-Service -Name px_service -ErrorAction SilentlyContinue
$processNames = @("px_service", "px_panel", "px_render", "px_function", "px_osinfo", "px_display")
$processes = foreach ($name in $processNames) {
    $instances = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
    [ordered]@{
        name = $name
        count = $instances.Count
        ids = @($instances.Id)
    }
}

$ports = foreach ($port in @(4999, 4601, 4603)) {
    $listeners = @(Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue)
    [ordered]@{
        port = $port
        listening = $listeners.Count -gt 0
        owning_processes = @($listeners.OwningProcess | Sort-Object -Unique)
    }
}

$parsecDevices = @(
    Get-PnpDevice -Class Display -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -eq "Parsec Virtual Display Adapter" } |
        ForEach-Object {
            [ordered]@{
                instance_id = $_.InstanceId
                status = $_.Status
                present = $_.Present
                problem = $_.Problem
            }
        }
)

$parsecDrivers = @(
    Get-CimInstance Win32_PnPSignedDriver -ErrorAction SilentlyContinue |
        Where-Object { $_.DeviceName -eq "Parsec Virtual Display Adapter" } |
        ForEach-Object {
            [ordered]@{
                device_id = $_.DeviceID
                driver_version = $_.DriverVersion
                inf_name = $_.InfName
                manufacturer = $_.Manufacturer
                is_signed = $_.IsSigned
                signer = $_.Signer
            }
        }
)

$statePath = "C:\Users\Public\Pixels\px_data\virtual_displays.json"
$virtualState = $null
if (Test-Path -LiteralPath $statePath) {
    try {
        $virtualState = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    } catch {
        $virtualState = [ordered]@{ parse_error = $_.Exception.Message }
    }
}

$uninstall = $uninstallKeys |
    ForEach-Object { Get-ItemProperty -LiteralPath $_ -ErrorAction SilentlyContinue } |
    Select-Object -First 1
$executables = foreach ($name in $processNames) {
    $path = Join-Path $InstallRoot "$name.exe"
    $item = Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
    [ordered]@{
        name = "$name.exe"
        present = $null -ne $item
        product_version = if ($item) { $item.VersionInfo.ProductVersion } else { $null }
    }
}

$result = [ordered]@{
    captured_at = (Get-Date).ToString("o")
    install_root = $InstallRoot
    installed = Test-Path -LiteralPath $InstallRoot
    installed_version = $uninstall.DisplayVersion
    service = [ordered]@{
        present = $null -ne $service
        status = if ($service) { $service.Status.ToString() } else { $null }
        start_type = if ($service) { $service.StartType.ToString() } else { $null }
    }
    processes = @($processes)
    ports = @($ports)
    executables = @($executables)
    parsec_vdd_devices = @($parsecDevices)
    parsec_vdd_drivers = @($parsecDrivers)
    virtual_display_state = $virtualState
}

[System.IO.File]::WriteAllText(
    $OutputPath,
    ($result | ConvertTo-Json -Depth 8),
    [System.Text.UTF8Encoding]::new($false)
)
