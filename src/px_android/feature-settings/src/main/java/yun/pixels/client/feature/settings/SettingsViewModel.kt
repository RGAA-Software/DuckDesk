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
import yun.pixels.client.core.domain.account.AccountRepository
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountState

class SettingsViewModel(private val accountRepository: AccountRepository) : ViewModel() {
    private val form = MutableStateFlow(SettingsUiState())
    val uiState = combine(form, accountRepository.state) { formState, accountState ->
        when (accountState) {
            AccountState.Loading -> formState.copy(isLoading = true, profile = null)
            AccountState.SignedOut -> formState.copy(isLoading = false, profile = null)
            is AccountState.SignedIn -> formState.copy(
                consoleEndpoint = accountState.session.endpoint.baseUrl,
                username = accountState.session.profile.username,
                password = "",
                isLoading = false,
                profile = accountState.session.profile,
            )
        }
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), SettingsUiState())

    fun onAction(action: SettingsAction) {
        when (action) {
            is SettingsAction.ConsoleEndpointChanged -> form.update { it.copy(consoleEndpoint = action.value, failure = null) }
            is SettingsAction.UsernameChanged -> form.update { it.copy(username = action.value, failure = null) }
            is SettingsAction.PasswordChanged -> form.update { it.copy(password = action.value, failure = null) }
            SettingsAction.Login -> login()
            SettingsAction.Logout -> logout()
            SettingsAction.DismissFailure -> form.update { it.copy(failure = null) }
        }
    }

    private fun login() {
        val request = form.value
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
            form.value = SettingsUiState(isLoading = false)
        }
    }

    companion object {
        fun factory(accountRepository: AccountRepository): ViewModelProvider.Factory = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T = SettingsViewModel(accountRepository) as T
        }
    }
}
