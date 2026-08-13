# UDP streaming network check script.
# Purpose: inspect physical NIC link speed, power-saving advanced properties,
#          and receive discard/error counters to locate occasional UDP packet loss.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File check_net_udp.ps1

$ErrorActionPreference = 'SilentlyContinue'

# Advanced properties relevant to UDP loss/latency
$KEY_PROPS = @(
    '*EEE',
    'EnableGreenEthernet',
    'PowerSavingMode',
    '*InterruptModeration',
    'GigaLite',
    'AutoDisableGigabit',
    '*ReceiveBuffers',
    '*SpeedDuplex'
)

# Exclude virtual adapters (VMware / Sangfor / TAP / loopback)
function Get-PhysicalAdapter {
    Get-NetAdapter | Where-Object {
        $_.InterfaceDescription -notmatch 'Virtual|VNIC|TAP|Loopback|SSL VPN'
    }
}

Write-Output "===== Physical NIC list ====="
Get-PhysicalAdapter |
    Select-Object Name, InterfaceDescription, Status, LinkSpeed, MacAddress |
    Format-Table -AutoSize

Write-Output "===== Key advanced properties (only Up physical NICs) ====="
foreach ($adapter in (Get-PhysicalAdapter | Where-Object { $_.Status -eq 'Up' })) {
    Write-Output "--- [$($adapter.Name)] $($adapter.InterfaceDescription) ---"
    Get-NetAdapterAdvancedProperty -Name $adapter.Name |
        Where-Object { $_.RegistryKeyword -in $KEY_PROPS } |
        Select-Object DisplayName, RegistryKeyword, DisplayValue |
        Format-Table -AutoSize
}

Write-Output "===== Receive discard / error counters ====="
Get-NetAdapterStatistics |
    Where-Object { $_.ReceivedDiscardedPackets -gt 0 -or $_.ReceivedPacketErrors -gt 0 } |
    Select-Object Name, ReceivedDiscardedPackets, ReceivedPacketErrors, OutboundDiscardedPackets, OutboundPacketErrors |
    Format-Table -AutoSize
