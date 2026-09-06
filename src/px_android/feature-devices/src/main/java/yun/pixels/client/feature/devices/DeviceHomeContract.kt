package yun.pixels.client.feature.devices

import yun.pixels.client.core.domain.device.RemoteDevice

enum class ConnectionInputError {
    Empty,
    Invalid,
    PublicNetworkAddress,
    Unreachable,
    InvalidResponse,
}

data class DeviceHomeUiState(
    val connectionInput: String = "",
    val inputError: ConnectionInputError? = null,
    val isConnecting: Boolean = false,
    val devices: List<RemoteDevice> = emptyList(),
)

enum class DeviceHomeNotice {
    DeviceSaved,
    DeviceRemoved,
    LocalNetworkPermissionRequired,
    FeatureUnavailable,
}

sealed interface DeviceHomeAction {
    data class ConnectionInputChanged(val value: String) : DeviceHomeAction

    object Connect : DeviceHomeAction
    object Paste : DeviceHomeAction
    object Scan : DeviceHomeAction
    object AddDevice : DeviceHomeAction
    object LocalNetworkPermissionDenied : DeviceHomeAction

    data class OpenDevice(val device: RemoteDevice) : DeviceHomeAction
    data class StartRemoteDesktop(val device: RemoteDevice) : DeviceHomeAction
    data class OpenFiles(val device: RemoteDevice) : DeviceHomeAction
    data class OpenApplications(val device: RemoteDevice) : DeviceHomeAction
    data class RemoveDevice(val device: RemoteDevice) : DeviceHomeAction
}
