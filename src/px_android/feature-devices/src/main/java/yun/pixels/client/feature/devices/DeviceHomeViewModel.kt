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
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import yun.pixels.client.core.domain.device.DeviceDirectory
import yun.pixels.client.core.domain.device.DeviceResolution
import yun.pixels.client.core.domain.device.DeviceResolutionFailure
import yun.pixels.client.core.domain.device.DeviceResolver

class DeviceHomeViewModel(
    private val deviceDirectory: DeviceDirectory,
    private val deviceResolver: DeviceResolver,
) : ViewModel() {
    private val editorState = MutableStateFlow(EditorState())
    private val mutableNotices = MutableSharedFlow<DeviceHomeNotice>(extraBufferCapacity = 1)

    val notices = mutableNotices.asSharedFlow()
    val uiState: StateFlow<DeviceHomeUiState> = combine(deviceDirectory.devices, editorState) { devices, editor ->
        DeviceHomeUiState(
            connectionInput = editor.connectionInput,
            inputError = editor.inputError,
            isConnecting = editor.isConnecting,
            devices = devices,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.Eagerly,
        initialValue = DeviceHomeUiState(),
    )

    fun onAction(action: DeviceHomeAction) {
        when (action) {
            is DeviceHomeAction.ConnectionInputChanged -> editorState.value = editorState.value.copy(
                connectionInput = action.value,
                inputError = null,
            )
            DeviceHomeAction.Connect -> connect()
            DeviceHomeAction.LocalNetworkPermissionDenied -> {
                mutableNotices.tryEmit(DeviceHomeNotice.LocalNetworkPermissionRequired)
            }
            is DeviceHomeAction.RemoveDevice -> viewModelScope.launch {
                deviceDirectory.remove(action.device.id)
                mutableNotices.emit(DeviceHomeNotice.DeviceRemoved)
            }
            DeviceHomeAction.Paste -> Unit
            else -> mutableNotices.tryEmit(DeviceHomeNotice.FeatureUnavailable)
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
    )

    companion object {
        fun factory(deviceDirectory: DeviceDirectory, deviceResolver: DeviceResolver): ViewModelProvider.Factory = viewModelFactory {
            initializer { DeviceHomeViewModel(deviceDirectory, deviceResolver) }
        }
    }
}

private fun DeviceResolutionFailure.asInputError(): ConnectionInputError = when (this) {
    DeviceResolutionFailure.InvalidInput -> ConnectionInputError.Invalid
    DeviceResolutionFailure.PublicNetworkAddress -> ConnectionInputError.PublicNetworkAddress
    DeviceResolutionFailure.Unreachable -> ConnectionInputError.Unreachable
    DeviceResolutionFailure.InvalidResponse -> ConnectionInputError.InvalidResponse
}
