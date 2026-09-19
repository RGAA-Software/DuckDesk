package yun.pixels.client.feature.settings

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import yun.pixels.client.core.domain.account.ConsoleSessionRepository
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ConsoleEndpoint

class SettingsViewModel(private val accountRepository: ConsoleSessionRepository) : ViewModel() {
    private val form = MutableStateFlow(SettingsUiState(endpointEditable = accountRepository.endpointEditable))
    val uiState = combine(form, accountRepository.state, accountRepository.endpoint, ::deriveUiState)
        .stateIn(
            viewModelScope,
            SharingStarted.WhileSubscribed(5_000),
            SettingsUiState(endpointEditable = accountRepository.endpointEditable),
        )

    private fun deriveUiState(
        formState: SettingsUiState,
        accountState: AccountState,
        endpoint: ConsoleEndpoint?,
    ): SettingsUiState {
        val endpointValue = if (formState.endpointEdited) formState.consoleEndpoint else endpoint?.baseUrl.orEmpty()
        return when (accountState) {
            AccountState.Loading -> formState.copy(consoleEndpoint = endpointValue, isLoading = true, profile = null)
            AccountState.SignedOut -> formState.copy(consoleEndpoint = endpointValue, isLoading = false, profile = null)
            is AccountState.SignedIn -> formState.copy(
                consoleEndpoint = endpointValue,
                username = accountState.session.profile.username,
                password = "",
                isLoading = false,
                profile = accountState.session.profile,
            )
        }
    }

    private fun currentUiState(): SettingsUiState = deriveUiState(form.value, accountRepository.state.value, accountRepository.endpoint.value)

    fun onAction(action: SettingsAction) {
        when (action) {
            is SettingsAction.ConsoleEndpointChanged -> if (accountRepository.endpointEditable) {
                form.update {
                    it.copy(
                        consoleEndpoint = action.value,
                        endpointEdited = true,
                        endpointTested = false,
                        failure = null,
                    )
                }
            }
            is SettingsAction.UsernameChanged -> form.update { it.copy(username = action.value, failure = null) }
            is SettingsAction.PasswordChanged -> form.update { it.copy(password = action.value, failure = null) }
            is SettingsAction.ConfirmPasswordChanged -> form.update { it.copy(confirmPassword = action.value, failure = null) }
            SettingsAction.SaveEndpoint -> saveEndpoint()
            SettingsAction.ConfirmEndpointChange -> saveEndpointNow()
            SettingsAction.CancelEndpointChange -> form.update { it.copy(confirmEndpointChange = false) }
            SettingsAction.TestEndpoint -> testEndpoint()
            SettingsAction.ToggleRegistration -> form.update {
                it.copy(registrationMode = !it.registrationMode, password = "", confirmPassword = "", failure = null)
            }
            SettingsAction.Login -> login()
            SettingsAction.Register -> register()
            SettingsAction.Logout -> logout()
            SettingsAction.DismissFailure -> form.update { it.copy(failure = null) }
        }
    }

    private fun saveEndpoint() {
        if (!accountRepository.endpointEditable) return
        val request = currentUiState()
        val endpoint = request.consoleEndpoint
        if (endpoint.isBlank()) return
        val signedInEndpoint = request.profile?.let { accountRepository.endpoint.value?.baseUrl }
        if (signedInEndpoint != null && canonicalEndpoint(endpoint) != canonicalEndpoint(signedInEndpoint)) {
            form.update { it.copy(confirmEndpointChange = true) }
            return
        }
        saveEndpointNow()
    }

    private fun saveEndpointNow() {
        if (!accountRepository.endpointEditable) return
        val endpoint = currentUiState().consoleEndpoint
        if (endpoint.isBlank()) return
        viewModelScope.launch {
            when (val result = accountRepository.saveEndpoint(endpoint)) {
                is AccountResult.Success -> form.update {
                    it.copy(
                        consoleEndpoint = result.value.baseUrl,
                        endpointTested = false,
                        endpointEdited = false,
                        confirmEndpointChange = false,
                        failure = null,
                    )
                }
                is AccountResult.Failure -> form.update { it.copy(confirmEndpointChange = false, failure = result.reason) }
            }
        }
    }

    private fun testEndpoint() {
        if (!accountRepository.endpointEditable) return
        val endpoint = currentUiState().consoleEndpoint
        if (endpoint.isBlank()) return
        viewModelScope.launch {
            form.update { it.copy(endpointTesting = true, endpointTested = false, failure = null) }
            when (val result = accountRepository.testEndpoint(endpoint)) {
                is AccountResult.Success -> form.update {
                    it.copy(
                        consoleEndpoint = result.value.baseUrl,
                        endpointTesting = false,
                        endpointTested = true,
                        endpointEdited = true,
                    )
                }
                is AccountResult.Failure -> form.update { it.copy(endpointTesting = false, failure = result.reason) }
            }
        }
    }

    private fun login() {
        val request = currentUiState()
        if (request.consoleEndpoint.isBlank() || request.username.isBlank() || request.password.isBlank()) return
        viewModelScope.launch {
            form.update { it.copy(isLoading = true, failure = null) }
            when (val result = accountRepository.login(request.consoleEndpoint, request.username, request.password)) {
                is AccountResult.Success -> form.update { it.copy(password = "", isLoading = false) }
                is AccountResult.Failure -> form.update { it.copy(password = "", isLoading = false, failure = result.reason) }
            }
        }
    }

    private fun logout() {
        viewModelScope.launch {
            form.update { it.copy(isLoading = true, failure = null) }
            accountRepository.logout()
            form.update { it.copy(username = "", password = "", confirmPassword = "", isLoading = false, failure = null) }
        }
    }

    private fun register() {
        val request = currentUiState()
        if (request.consoleEndpoint.isBlank() || request.username.isBlank() || request.password.length < 8 ||
            request.password != request.confirmPassword
        ) return
        viewModelScope.launch {
            form.update { it.copy(isLoading = true, failure = null) }
            when (val saved = accountRepository.saveEndpoint(request.consoleEndpoint)) {
                is AccountResult.Failure -> form.update { it.copy(isLoading = false, failure = saved.reason) }
                is AccountResult.Success -> when (val result = accountRepository.register(request.username, request.password)) {
                    is AccountResult.Success -> form.update {
                        it.copy(password = "", confirmPassword = "", registrationMode = false, isLoading = false)
                    }
                    is AccountResult.Failure -> form.update {
                        it.copy(password = "", confirmPassword = "", isLoading = false, failure = result.reason)
                    }
                }
            }
        }
    }

    companion object {
        fun factory(accountRepository: ConsoleSessionRepository): ViewModelProvider.Factory = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T = SettingsViewModel(accountRepository) as T
        }
    }

    private fun canonicalEndpoint(value: String): String = value.trim().trimEnd('/')
}
