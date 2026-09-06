package yun.pixels.client.feature.devices

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import java.util.UUID
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.account.RemoteApplication

data class ApplicationLibraryUiState(
    val applications: List<RemoteApplication> = emptyList(),
    val loading: Boolean = false,
    val pendingAppId: String? = null,
    val failure: AccountFailure? = null,
)

class ApplicationLibraryViewModel(private val repository: ApplicationRepository) : ViewModel() {
    private val mutableState = MutableStateFlow(ApplicationLibraryUiState())
    val state: StateFlow<ApplicationLibraryUiState> = mutableState.asStateFlow()

    fun refresh() {
        if (mutableState.value.loading) return
        viewModelScope.launch { load() }
    }

    fun start(application: RemoteApplication) {
        if (mutableState.value.pendingAppId != null) return
        viewModelScope.launch {
            mutableState.value = mutableState.value.copy(pendingAppId = application.appId, failure = null)
            when (val result = repository.start(application.appId, UUID.randomUUID().toString())) {
                is AccountResult.Success -> load(pendingAppId = null)
                is AccountResult.Failure -> mutableState.value = mutableState.value.copy(pendingAppId = null, failure = result.reason)
            }
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

    companion object {
        fun factory(repository: ApplicationRepository): ViewModelProvider.Factory = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T = ApplicationLibraryViewModel(repository) as T
        }
    }
}
