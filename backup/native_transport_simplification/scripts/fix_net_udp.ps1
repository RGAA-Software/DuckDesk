# UDP streaming network optimization script.
# Purpose: disable power-saving options (EEE / Green Ethernet / Power Saving Mode)
#          and interrupt moderation on the physical NIC. These are common causes
#          of occasional packet loss / latency in real-time UDP on Realtek NICs.
# Usage: run as administrator
#        powershell -NoProfile -ExecutionPolicy Bypass -File fix_net_udp.ps1
# Tip: run check_net_udp.ps1 before and after to verify.

$ErrorActionPreference = 'Continue'

# Properties to disable (RegistryKeyword, DisplayName)
$DISABLE_PROPS = @(
    @{ Keyword = '*EEE';                 Name = 'Energy Efficient Ethernet (*EEE)' },
    @{ Keyword = 'EnableGreenEthernet';  Name = 'Green Ethernet' },
    @{ Keyword = 'PowerSavingMode';      Name = 'Power Saving Mode' },
    @{ Keyword = '*InterruptModeration'; Name = 'Interrupt Moderation' }
)

# Exclude virtual adapters
function Get-PhysicalAdapter {
    Get-NetAdapter | Where-Object {
        $_.InterfaceDescription -notmatch 'Virtual|VNIC|TAP|Loopback|SSL VPN'
    }
}

$adapters = Get-PhysicalAdapter | Where-Object { $_.Status -eq 'Up' }
if (-not $adapters) {
    Write-Output "No Up physical NIC found. Exiting."
    exit 1
}

foreach ($adapter in $adapters) {
    Write-Output "===== Processing [$($adapter.Name)] $($adapter.InterfaceDescription) ====="
    foreach ($prop in $DISABLE_PROPS) {
        try {
            $cur = Get-NetAdapterAdvancedProperty -Name $adapter.Name -RegistryKeyword $prop.Keyword -ErrorAction Stop
            if ($cur.RegistryValue -eq '0') {
                Write-Output ("  [skip] {0}: already disabled" -f $prop.Name)
            }
            else {
                Set-NetAdapterAdvancedProperty -Name $adapter.Name -RegistryKeyword $prop.Keyword -RegistryValue '0' -ErrorAction Stop
                Write-Output ("  [ok] {0}: disabled (was {1})" -f $prop.Name, $cur.DisplayValue)
            }
        }
        catch {
            Write-Output ("  [fail] {0}: {1}" -f $prop.Name, $_.Exception.Message)
        }
    }
}

Write-Output ""
Write-Output "===== State after change ====="
foreach ($adapter in (Get-PhysicalAdapter | Where-Object { $_.Status -eq 'Up' })) {
    Write-Output "--- [$($adapter.Name)] ---"
    Get-NetAdapterAdvancedProperty -Name $adapter.Name |
        Where-Object { $_.RegistryKeyword -in ($DISABLE_PROPS | ForEach-Object { $_.Keyword }) } |
        Select-Object DisplayName, RegistryKeyword, DisplayValue |
        Format-Table -AutoSize
}
