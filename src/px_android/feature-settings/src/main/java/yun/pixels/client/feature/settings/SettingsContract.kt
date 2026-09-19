package yun.pixels.client.feature.settings

import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountProfile

data class SettingsUiState(
    val consoleEndpoint: String = "",
    val username: String = "",
    val password: String = "",
    val confirmPassword: String = "",
    val registrationMode: Boolean = false,
    val isLoading: Boolean = true,
    val endpointTesting: Boolean = false,
    val endpointTested: Boolean = false,
    val endpointEdited: Boolean = false,
    val endpointEditable: Boolean = true,
    val confirmEndpointChange: Boolean = false,
    val profile: AccountProfile? = null,
    val failure: AccountFailure? = null,
)

sealed interface SettingsAction {
    data class ConsoleEndpointChanged(val value: String) : SettingsAction

    data class UsernameChanged(val value: String) : SettingsAction

    data class PasswordChanged(val value: String) : SettingsAction

    data class ConfirmPasswordChanged(val value: String) : SettingsAction

    data object SaveEndpoint : SettingsAction

    data object ConfirmEndpointChange : SettingsAction

    data object CancelEndpointChange : SettingsAction

    data object TestEndpoint : SettingsAction

    data object ToggleRegistration : SettingsAction

    data object Login : SettingsAction

    data object Register : SettingsAction

    data object Logout : SettingsAction

    data object DismissFailure : SettingsAction
}
