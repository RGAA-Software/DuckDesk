package yun.pixels.client.feature.settings

import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountProfile

data class SettingsUiState(
    val consoleEndpoint: String = "",
    val username: String = "",
    val password: String = "",
    val isLoading: Boolean = true,
    val profile: AccountProfile? = null,
    val failure: AccountFailure? = null,
)

sealed interface SettingsAction {
    data class ConsoleEndpointChanged(val value: String) : SettingsAction

    data class UsernameChanged(val value: String) : SettingsAction

    data class PasswordChanged(val value: String) : SettingsAction

    data object Login : SettingsAction

    data object Logout : SettingsAction

    data object DismissFailure : SettingsAction
}
