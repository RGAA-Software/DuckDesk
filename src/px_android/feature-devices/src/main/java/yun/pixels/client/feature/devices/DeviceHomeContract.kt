package yun.pixels.client.feature.devices

import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountProfile
import yun.pixels.client.core.domain.account.AccountState
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
    val isDiscovering: Boolean = false,
    val devices: List<RemoteDevice> = emptyList(),
    val account: AccountSummary = AccountSummary.SignedOut,
    val accountDevices: List<AccountDevice> = emptyList(),
    val isRefreshingAccountDevices: Boolean = false,
    val accountFailure: AccountFailure? = null,
)

sealed interface AccountSummary {
    data object SignedOut : AccountSummary

    data object Loading : AccountSummary

    data class SignedIn(val profile: AccountProfile) : AccountSummary
}

internal fun AccountState.asSummary(): AccountSummary = when (this) {
    AccountState.SignedOut -> AccountSummary.SignedOut
    AccountState.Loading -> AccountSummary.Loading
    is AccountState.SignedIn -> AccountSummary.SignedIn(session.profile)
}

enum class DeviceHomeNotice {
    DeviceSaved,
    DeviceRemoved,
    LocalNetworkPermissionRequired,
    FeatureUnavailable,
    DiscoveryFinished,
    NoDevicesDiscovered,
    ScannerUnavailable,
}

sealed interface DeviceHomeAction {
    data class ConnectionInputChanged(val value: String) : DeviceHomeAction

    object Connect : DeviceHomeAction
    object Paste : DeviceHomeAction
    object ScanCode : DeviceHomeAction
    object DiscoverLocal : DeviceHomeAction
    object ScannerFailed : DeviceHomeAction
    object OpenAccountSettings : DeviceHomeAction
    object RefreshAccountDevices : DeviceHomeAction
    object LocalNetworkPermissionDenied : DeviceHomeAction

    data class OpenDevice(val device: RemoteDevice) : DeviceHomeAction
    data class OpenAccountDevice(val device: AccountDevice) : DeviceHomeAction
    data class StartAccountRemoteDesktop(val device: AccountDevice) : DeviceHomeAction
    data class StartRemoteDesktop(val device: RemoteDevice) : DeviceHomeAction
    data class OpenFiles(val device: RemoteDevice) : DeviceHomeAction
    data class OpenApplications(val device: RemoteDevice) : DeviceHomeAction
    data class RemoveDevice(val device: RemoteDevice) : DeviceHomeAction
}
