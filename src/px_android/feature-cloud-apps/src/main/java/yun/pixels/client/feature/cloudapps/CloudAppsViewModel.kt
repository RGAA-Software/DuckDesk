package yun.pixels.client.feature.cloudapps

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
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.account.ConsoleSessionRepository
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionTarget

data class CloudAppsUiState(
    val applications: List<RemoteApplication> = emptyList(),
    val loading: Boolean = false,
    val pendingAppId: String? = null,
    val failure: AccountFailure? = null,
    val endpointConfigured: Boolean = false,
    val signedIn: Boolean = false,
)

class CloudAppsViewModel(
    private val repository: ApplicationRepository,
    private val sessions: ConsoleSessionRepository,
) : ViewModel() {
    private val mutableState = MutableStateFlow(CloudAppsUiState())
    private val mutableRemoteRequests = MutableSharedFlow<RemoteSessionRequest>(extraBufferCapacity = 1)
    val state: StateFlow<CloudAppsUiState> = mutableState.asStateFlow()
    val remoteRequests = mutableRemoteRequests.asSharedFlow()

    init {
        viewModelScope.launch {
            combine(sessions.endpoint, sessions.state) { endpoint, accountState -> endpoint to accountState }
                .distinctUntilChanged()
                .collect { (endpoint, accountState) ->
                    syncAccountState(endpoint != null, accountState is AccountState.SignedIn)
                    if (endpoint == null) {
                        mutableState.value = CloudAppsUiState(endpointConfigured = false)
                    } else {
                        load()
                    }
                }
        }
    }

    fun refresh() { if (!mutableState.value.loading) viewModelScope.launch { load() } }

    fun start(application: RemoteApplication) {
        if (!application.isSupported || mutableState.value.pendingAppId != null) return
        viewModelScope.launch {
            mutableState.value = mutableState.value.copy(pendingAppId = application.appId, failure = null)
            val nonce = UUID.randomUUID().toString()
            when (val result = repository.start(application.appId, nonce)) {
                is AccountResult.Success -> awaitReadyAndConnect(application, result.value, nonce)
                is AccountResult.Failure -> fail(result.reason)
            }
        }
    }

    fun connect(application: RemoteApplication) {
        if (!application.isSupported || mutableState.value.pendingAppId != null) return
        val instance = application.runningInstance?.takeIf(RemoteApplicationInstance::reconnectable) ?: return
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
                is AccountResult.Success -> load()
                is AccountResult.Failure -> fail(result.reason)
            }
        }
    }

    private fun syncAccountState(endpointConfigured: Boolean, signedIn: Boolean) {
        mutableState.value = mutableState.value.copy(
            endpointConfigured = endpointConfigured,
            signedIn = signedIn,
        )
    }

    private suspend fun load() {
        if (sessions.endpoint.value == null) {
            mutableState.value = CloudAppsUiState(endpointConfigured = false)
            return
        }
        mutableState.value = mutableState.value.copy(loading = true, failure = null)
        mutableState.value = when (val result = repository.applications()) {
            is AccountResult.Success -> mutableState.value.copy(applications = result.value, loading = false, pendingAppId = null)
            is AccountResult.Failure -> mutableState.value.copy(loading = false, pendingAppId = null, failure = result.reason)
        }
    }

    private suspend fun connect(application: RemoteApplication, instance: RemoteApplicationInstance, nonce: String) {
        when (val result = repository.resolveConnection(application.appId, instance.instanceId)) {
            is AccountResult.Failure -> fail(result.reason)
            is AccountResult.Success -> {
                mutableState.value = mutableState.value.copy(pendingAppId = null)
                mutableRemoteRequests.emit(
                    RemoteSessionRequest(
                        RemoteSessionId(UUID.randomUUID().toString()),
                        RemoteSessionTarget.CloudApplication(application.name, application.appId, instance.instanceId, result.value),
                    ),
                )
            }
        }
    }

    private suspend fun awaitReadyAndConnect(application: RemoteApplication, initial: RemoteApplicationInstance, nonce: String) {
        var instance = initial
        repeat(20) { attempt ->
            if (instance.reconnectable) { connect(application, instance, nonce); return }
            if (instance.state == RemoteApplicationInstance.State.Failed || instance.state == RemoteApplicationInstance.State.Stopped) {
                load(); return
            }
            if (attempt < 19) delay(500)
            val refreshed = repository.applications()
            if (refreshed is AccountResult.Failure) { fail(refreshed.reason); return }
            val applications = (refreshed as AccountResult.Success).value
            mutableState.value = mutableState.value.copy(applications = applications, loading = false)
            instance = applications.firstOrNull { it.appId == application.appId }?.runningInstance ?: run {
                fail(AccountFailure.NotFound); return
            }
        }
        fail(AccountFailure.ServerError)
    }

    private fun fail(reason: AccountFailure) {
        mutableState.value = mutableState.value.copy(loading = false, pendingAppId = null, failure = reason)
    }

    companion object {
        fun factory(repository: ApplicationRepository, sessions: ConsoleSessionRepository): ViewModelProvider.Factory =
            object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T = CloudAppsViewModel(repository, sessions) as T
            }
    }
}
