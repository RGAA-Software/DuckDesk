$ErrorActionPreference = "Stop"

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
    throw "USBMMIDD driver package remains in Driver Store: $($remaining.Driver -join ', ')"
}

exit 0
