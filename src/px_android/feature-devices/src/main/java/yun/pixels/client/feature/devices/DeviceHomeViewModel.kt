package yun.pixels.client.feature.devices

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountRepository
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.device.DeviceDirectory
import yun.pixels.client.core.domain.device.DeviceDiscovery
import yun.pixels.client.core.domain.device.DeviceResolution
import yun.pixels.client.core.domain.device.DeviceResolutionFailure
import yun.pixels.client.core.domain.device.DeviceResolver

class DeviceHomeViewModel(
    private val deviceDirectory: DeviceDirectory,
    private val deviceResolver: DeviceResolver,
    private val deviceDiscovery: DeviceDiscovery,
    private val accountRepository: AccountRepository,
) : ViewModel() {
    private val editorState = MutableStateFlow(EditorState())
    private val accountDevicesState = MutableStateFlow(AccountDevicesState())
    private val mutableNotices = MutableSharedFlow<DeviceHomeNotice>(extraBufferCapacity = 1)

    val notices = mutableNotices.asSharedFlow()
    val uiState: StateFlow<DeviceHomeUiState> = combine(
        deviceDirectory.devices,
        editorState,
        accountRepository.state,
        accountDevicesState,
    ) { devices, editor, account, cloud ->
        DeviceHomeUiState(
            connectionInput = editor.connectionInput,
            inputError = editor.inputError,
            isConnecting = editor.isConnecting,
            isDiscovering = editor.isDiscovering,
            devices = devices,
            account = account.asSummary(),
            accountDevices = cloud.devices,
            isRefreshingAccountDevices = cloud.isRefreshing,
            accountFailure = cloud.failure,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.Eagerly,
        initialValue = DeviceHomeUiState(),
    )

    init {
        viewModelScope.launch {
            accountRepository.state.collectLatest { state ->
                if (state is AccountState.SignedIn) {
                    refreshAccountDevicesNow()
                } else {
                    accountDevicesState.value = AccountDevicesState()
                }
            }
        }
    }

    fun onAction(action: DeviceHomeAction) {
        when (action) {
            is DeviceHomeAction.ConnectionInputChanged -> editorState.value = editorState.value.copy(
                connectionInput = action.value,
                inputError = null,
            )
            DeviceHomeAction.Connect -> connect()
            DeviceHomeAction.RefreshAccountDevices -> refreshAccountDevices()
            DeviceHomeAction.DiscoverLocal -> discoverDevices()
            DeviceHomeAction.LocalNetworkPermissionDenied -> {
                mutableNotices.tryEmit(DeviceHomeNotice.LocalNetworkPermissionRequired)
            }
            DeviceHomeAction.ScannerFailed -> mutableNotices.tryEmit(DeviceHomeNotice.ScannerUnavailable)
            is DeviceHomeAction.RemoveDevice -> viewModelScope.launch {
                deviceDirectory.remove(action.device.id)
                mutableNotices.emit(DeviceHomeNotice.DeviceRemoved)
            }
            DeviceHomeAction.Paste -> Unit
            DeviceHomeAction.ScanCode -> Unit
            DeviceHomeAction.OpenAccountSettings -> Unit
            else -> mutableNotices.tryEmit(DeviceHomeNotice.FeatureUnavailable)
        }
    }

    private fun refreshAccountDevices() {
        if (accountDevicesState.value.isRefreshing) return
        viewModelScope.launch { refreshAccountDevicesNow() }
    }

    private fun discoverDevices() {
        if (editorState.value.isDiscovering) return
        viewModelScope.launch {
            editorState.value = editorState.value.copy(isDiscovering = true)
            val devices = runCatching { deviceDiscovery.discover() }.getOrDefault(emptyList())
            devices.forEach { deviceDirectory.save(it) }
            editorState.value = editorState.value.copy(isDiscovering = false)
            mutableNotices.emit(if (devices.isEmpty()) DeviceHomeNotice.NoDevicesDiscovered else DeviceHomeNotice.DiscoveryFinished)
        }
    }

    private suspend fun refreshAccountDevicesNow() {
        accountDevicesState.value = accountDevicesState.value.copy(isRefreshing = true, failure = null)
        accountDevicesState.value = when (val result = accountRepository.devices()) {
            is AccountResult.Success -> AccountDevicesState(devices = result.value)
            is AccountResult.Failure -> AccountDevicesState(failure = result.reason)
        }
    }

    private fun connect() {
        val input = editorState.value.connectionInput.trim()
        if (input.isEmpty()) {
            editorState.value = editorState.value.copy(inputError = ConnectionInputError.Empty)
            return
        }
        if (editorState.value.isConnecting) return

        viewModelScope.launch {
            editorState.value = editorState.value.copy(isConnecting = true, inputError = null)
            try {
                when (val resolution = deviceResolver.resolve(input)) {
                    is DeviceResolution.Success -> {
                        deviceDirectory.save(resolution.resolvedDevice)
                        editorState.value = EditorState()
                        mutableNotices.emit(DeviceHomeNotice.DeviceSaved)
                    }
                    is DeviceResolution.Failure -> editorState.value = editorState.value.copy(
                        isConnecting = false,
                        inputError = resolution.reason.asInputError(),
                    )
                }
            } catch (_: Exception) {
                editorState.value = editorState.value.copy(
                    isConnecting = false,
                    inputError = ConnectionInputError.Unreachable,
                )
            }
        }
    }

    private data class EditorState(
        val connectionInput: String = "",
        val inputError: ConnectionInputError? = null,
        val isConnecting: Boolean = false,
        val isDiscovering: Boolean = false,
    )

    private data class AccountDevicesState(
        val devices: List<AccountDevice> = emptyList(),
        val isRefreshing: Boolean = false,
        val failure: AccountFailure? = null,
    )

    companion object {
        fun factory(
            deviceDirectory: DeviceDirectory,
            deviceResolver: DeviceResolver,
            deviceDiscovery: DeviceDiscovery,
            accountRepository: AccountRepository,
        ): ViewModelProvider.Factory = viewModelFactory {
            initializer { DeviceHomeViewModel(deviceDirectory, deviceResolver, deviceDiscovery, accountRepository) }
        }
    }
}

private fun DeviceResolutionFailure.asInputError(): ConnectionInputError = when (this) {
    DeviceResolutionFailure.InvalidInput -> ConnectionInputError.Invalid
    DeviceResolutionFailure.PublicNetworkAddress -> ConnectionInputError.PublicNetworkAddress
    DeviceResolutionFailure.Unreachable -> ConnectionInputError.Unreachable
    DeviceResolutionFailure.InvalidResponse -> ConnectionInputError.InvalidResponse
}
