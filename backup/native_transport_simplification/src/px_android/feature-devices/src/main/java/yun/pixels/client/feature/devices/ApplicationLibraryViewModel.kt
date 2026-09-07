package yun.pixels.client.feature.devices

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import java.util.UUID
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.account.JoinMode
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionTarget

data class ApplicationLibraryUiState(
    val applications: List<RemoteApplication> = emptyList(),
    val loading: Boolean = false,
    val pendingAppId: String? = null,
    val failure: AccountFailure? = null,
)

class ApplicationLibraryViewModel(private val repository: ApplicationRepository) : ViewModel() {
    private val mutableState = MutableStateFlow(ApplicationLibraryUiState())
    private val mutableRemoteRequests = MutableSharedFlow<RemoteSessionRequest>(extraBufferCapacity = 1)
    val state: StateFlow<ApplicationLibraryUiState> = mutableState.asStateFlow()
    val remoteRequests = mutableRemoteRequests.asSharedFlow()

    fun refresh() {
        if (mutableState.value.loading) return
        viewModelScope.launch { load() }
    }

    fun start(application: RemoteApplication) {
        if (mutableState.value.pendingAppId != null) return
        viewModelScope.launch {
            mutableState.value = mutableState.value.copy(pendingAppId = application.appId, failure = null)
            val clientNonce = UUID.randomUUID().toString()
            when (val result = repository.start(application.appId, clientNonce)) {
                is AccountResult.Success -> {
                    awaitReadyAndConnect(application, result.value, clientNonce)
                }
                is AccountResult.Failure -> mutableState.value = mutableState.value.copy(pendingAppId = null, failure = result.reason)
            }
        }
    }

    fun connect(application: RemoteApplication) {
        val instance = application.runningInstance?.takeIf(RemoteApplicationInstance::reconnectable) ?: return
        if (mutableState.value.pendingAppId != null) return
        viewModelScope.launch {
            mutableState.value = mutableState.value.copy(pendingAppId = application.appId, failure = null)
            connect(application, instance, UUID.randomUUID().toString())
        }
    }

    fun stop(application: RemoteApplication) {
        val instance = application.runningInstance ?: return
        if (mutableState.value.pendingAppId != null) return
        viewModelScope.launch {
            mutableState.value = mutableState.value.copy(pendingAppId = application.appId, failure = null)
            when (val result = repository.stop(instance.instanceId)) {
                is AccountResult.Success -> load(pendingAppId = null)
                is AccountResult.Failure -> mutableState.value = mutableState.value.copy(pendingAppId = null, failure = result.reason)
            }
        }
    }

    private suspend fun load(pendingAppId: String? = mutableState.value.pendingAppId) {
        mutableState.value = mutableState.value.copy(loading = true, pendingAppId = pendingAppId, failure = null)
        mutableState.value = when (val result = repository.applications()) {
            is AccountResult.Success -> ApplicationLibraryUiState(applications = result.value, pendingAppId = pendingAppId)
            is AccountResult.Failure -> ApplicationLibraryUiState(failure = result.reason, pendingAppId = pendingAppId)
        }
    }

    private suspend fun connect(
        application: RemoteApplication,
        instance: RemoteApplicationInstance,
        clientNonce: String,
    ) {
        when (val result = repository.issueTicket(instance.instanceId, clientNonce, JoinMode.Control)) {
            is AccountResult.Success -> {
                mutableState.value = mutableState.value.copy(
                    applications = mutableState.value.applications.map { item ->
                        if (item.appId == application.appId) item.copy(runningInstance = instance) else item
                    },
                    pendingAppId = null,
                    failure = null,
                )
                mutableRemoteRequests.emit(
                    RemoteSessionRequest(
                        id = RemoteSessionId(result.value.logicalSessionId.ifBlank { UUID.randomUUID().toString() }),
                        target = RemoteSessionTarget.Account(
                            displayName = application.name,
                            fallbackRemoteDeviceId = "",
                            connectionTicket = result.value,
                            clientNonce = clientNonce,
                        ),
                    ),
                )
            }
            is AccountResult.Failure -> mutableState.value = mutableState.value.copy(pendingAppId = null, failure = result.reason)
        }
    }

    private suspend fun awaitReadyAndConnect(
        application: RemoteApplication,
        initialInstance: RemoteApplicationInstance,
        clientNonce: String,
    ) {
        var instance = initialInstance
        repeat(APPLICATION_READY_ATTEMPTS) { attempt ->
            if (instance.reconnectable) {
                connect(application, instance, clientNonce)
                return
            }
            if (instance.state in setOf(RemoteApplicationInstance.State.Stopped, RemoteApplicationInstance.State.Failed)) {
                load(pendingAppId = null)
                return
            }
            if (attempt + 1 < APPLICATION_READY_ATTEMPTS) delay(APPLICATION_READY_POLL_MILLIS)
            when (val result = repository.applications()) {
                is AccountResult.Success -> {
                    mutableState.value = mutableState.value.copy(applications = result.value, loading = false)
                    instance = result.value.firstOrNull { it.appId == application.appId }?.runningInstance ?: run {
                        mutableState.value = mutableState.value.copy(pendingAppId = null, failure = AccountFailure.NotFound)
                        return
                    }
                }
                is AccountResult.Failure -> {
                    mutableState.value = mutableState.value.copy(pendingAppId = null, failure = result.reason)
                    return
                }
            }
        }
        if (instance.reconnectable) {
            connect(application, instance, clientNonce)
        } else {
            mutableState.value = mutableState.value.copy(pendingAppId = null, failure = AccountFailure.ServerError)
        }
    }

    companion object {
        private const val APPLICATION_READY_ATTEMPTS = 20
        private const val APPLICATION_READY_POLL_MILLIS = 500L

        fun factory(repository: ApplicationRepository): ViewModelProvider.Factory = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T = ApplicationLibraryViewModel(repository) as T
        }
    }
}
