param(
    [Parameter(Mandatory = $true)]
    [Alias("ResultPath")]
    [string]$OutputPath,

    [string]$InstallRoot = "C:\Program Files\PixelsRender",

    # Optional compatibility parameters let this read-only audit reuse the
    # silent-installer scheduled-task entry point on locked-down test hosts.
    [string]$Mode,
    [string]$Executable
)

$ErrorActionPreference = "Stop"

$uninstallKeys = @(
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Pixels px_panel",
    "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Pixels px_panel"
)
$service = Get-Service -Name px_service -ErrorAction SilentlyContinue
$processNames = @("px_service", "px_panel", "px_render", "px_function", "px_osinfo")
$processes = foreach ($name in $processNames) {
    $instances = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
    [ordered]@{
        name = $name
        count = $instances.Count
        ids = @($instances.Id)
    }
}

$ports = foreach ($port in @(20369, 20371, 20375)) {
    $listeners = @(Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue)
    [ordered]@{
        port = $port
        listening = $listeners.Count -gt 0
        owning_processes = @($listeners.OwningProcess | Sort-Object -Unique)
    }
}

$usbDevices = @(
    Get-PnpDevice -Class Display -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -eq "USB Mobile Monitor Virtual Display" } |
        ForEach-Object {
            [ordered]@{
                instance_id = $_.InstanceId
                status = $_.Status
                present = $_.Present
                problem = $_.Problem
            }
        }
)

$usbDrivers = @(
    Get-CimInstance Win32_PnPSignedDriver -ErrorAction SilentlyContinue |
        Where-Object { $_.DeviceName -eq "USB Mobile Monitor Virtual Display" } |
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
    usbmmidd_devices = @($usbDevices)
    usbmmidd_drivers = @($usbDrivers)
    virtual_display_state = $virtualState
}

[System.IO.File]::WriteAllText(
    $OutputPath,
    ($result | ConvertTo-Json -Depth 8),
    [System.Text.UTF8Encoding]::new($false)
)
