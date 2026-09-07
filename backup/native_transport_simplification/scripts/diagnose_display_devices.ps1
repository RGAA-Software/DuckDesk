param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$source = @'
using System;
using System.Runtime.InteropServices;

public static class DisplayDeviceNative
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct DISPLAY_DEVICE
    {
        public int cb;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceString;
        public int StateFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceID;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceKey;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool EnumDisplayDevices(
        string lpDevice,
        uint iDevNum,
        ref DISPLAY_DEVICE lpDisplayDevice,
        uint dwFlags);
}
'@

Add-Type -TypeDefinition $source

$items = @()
for ($adapterIndex = 0; ; $adapterIndex++) {
    $adapter = New-Object DisplayDeviceNative+DISPLAY_DEVICE
    $adapter.cb = [Runtime.InteropServices.Marshal]::SizeOf($adapter)
    if (-not [DisplayDeviceNative]::EnumDisplayDevices($null, $adapterIndex, [ref]$adapter, 0)) {
        break
    }

    $children = @()
    for ($childIndex = 0; ; $childIndex++) {
        $child = New-Object DisplayDeviceNative+DISPLAY_DEVICE
        $child.cb = [Runtime.InteropServices.Marshal]::SizeOf($child)
        if (-not [DisplayDeviceNative]::EnumDisplayDevices($adapter.DeviceName, $childIndex, [ref]$child, 0)) {
            break
        }
        $children += [ordered]@{
            index = $childIndex
            name = $child.DeviceName
            string = $child.DeviceString
            flags = ('0x{0:X8}' -f $child.StateFlags)
            id = $child.DeviceID
            key = $child.DeviceKey
        }
    }

    $items += [ordered]@{
        index = $adapterIndex
        name = $adapter.DeviceName
        string = $adapter.DeviceString
        flags = ('0x{0:X8}' -f $adapter.StateFlags)
        id = $adapter.DeviceID
        key = $adapter.DeviceKey
        children = $children
    }
}

$direct = @()
foreach ($displayNumber in 1..64) {
    $displayName = "\\.\DISPLAY$displayNumber"
    $children = @()
    for ($childIndex = 0; ; $childIndex++) {
        $child = New-Object DisplayDeviceNative+DISPLAY_DEVICE
        $child.cb = [Runtime.InteropServices.Marshal]::SizeOf($child)
        if (-not [DisplayDeviceNative]::EnumDisplayDevices($displayName, $childIndex, [ref]$child, 0)) {
            break
        }
        $children += [ordered]@{
            index = $childIndex
            name = $child.DeviceName
            string = $child.DeviceString
            flags = ('0x{0:X8}' -f $child.StateFlags)
            id = $child.DeviceID
            key = $child.DeviceKey
        }
    }
    if ($children.Count -gt 0) {
        $direct += [ordered]@{ name = $displayName; children = $children }
    }
}

[ordered]@{
    session_id = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
    captured_at = (Get-Date).ToString('o')
    devices = $items
    direct_children = $direct
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
