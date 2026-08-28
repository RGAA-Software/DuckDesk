$ErrorActionPreference = "Stop"

$devices = @(
    Get-PnpDevice -Class Display -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -eq "USB Mobile Monitor Virtual Display" }
)

foreach ($device in $devices) {
    $process = Start-Process `
        -FilePath "$env:SystemRoot\System32\pnputil.exe" `
        -ArgumentList @("/remove-device", $device.InstanceId) `
        -Wait `
        -PassThru `
        -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "pnputil failed to remove legacy USBMMIDD device $($device.InstanceId): exit $($process.ExitCode)"
    }
}

$packages = @(
    Get-WindowsDriver -Online -All |
        Where-Object {
            $_.ProviderName -eq "Amyuni" -and
            [System.IO.Path]::GetFileName($_.OriginalFileName) -ieq "usbmmIdd.inf"
        }
)

foreach ($package in $packages) {
    $process = Start-Process `
        -FilePath "$env:SystemRoot\System32\pnputil.exe" `
        -ArgumentList @("/delete-driver", $package.Driver, "/uninstall", "/force") `
        -Wait `
        -PassThru `
        -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "pnputil failed to delete $($package.Driver): exit $($process.ExitCode)"
    }
}

$remaining = @(
    Get-WindowsDriver -Online -All |
        Where-Object {
            $_.ProviderName -eq "Amyuni" -and
            [System.IO.Path]::GetFileName($_.OriginalFileName) -ieq "usbmmIdd.inf"
        }
)

if ($remaining.Count -ne 0) {
    throw "Legacy USBMMIDD driver package remains in Driver Store: $($remaining.Driver -join ', ')"
}

exit 0
