@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package yun.pixels.client.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import yun.pixels.client.core.domain.account.AccountFailure

@Composable
fun SettingsScreen(
    state: SettingsUiState,
    applicationName: String = "Application",
    appVersion: String = "",
    onAction: (SettingsAction) -> Unit,
    onExportDiagnostics: () -> Unit = {},
) {
    var informationDialog by rememberSaveable { mutableStateOf<InformationDialog?>(null) }
    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(title = { Text(stringResource(R.string.settings_title), fontWeight = FontWeight.SemiBold) })
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Text(stringResource(R.string.console_title), style = MaterialTheme.typography.titleLarge)
            ConsoleEndpointForm(state, onAction)
            Text(stringResource(R.string.account_title, applicationName), style = MaterialTheme.typography.titleLarge)
            when {
                state.isLoading -> CircularProgressIndicator(modifier = Modifier.align(Alignment.CenterHorizontally))
                state.profile != null -> SignedInAccount(state, onAction)
                else -> LoginForm(state, onAction)
            }
            Text(stringResource(R.string.security_title), style = MaterialTheme.typography.titleMedium)
            Text(
                stringResource(R.string.security_body),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyMedium,
            )
            Text(stringResource(R.string.about_title, applicationName), style = MaterialTheme.typography.titleMedium)
            if (appVersion.isNotBlank()) {
                Text(
                    stringResource(R.string.version_format, appVersion),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
            Text(stringResource(R.string.software_update_title), style = MaterialTheme.typography.titleMedium)
            UpdateControls(state, onAction)
            OutlinedButton(onClick = { informationDialog = InformationDialog.Privacy }, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(R.string.privacy_title))
            }
            OutlinedButton(onClick = { informationDialog = InformationDialog.OpenSource }, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(R.string.open_source_title))
            }
            OutlinedButton(onClick = onExportDiagnostics, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(R.string.export_diagnostics))
            }
        }
    }
    informationDialog?.let { dialog ->
        val title = if (dialog == InformationDialog.Privacy) R.string.privacy_title else R.string.open_source_title
        val body = if (dialog == InformationDialog.Privacy) {
            stringResource(R.string.privacy_body, applicationName)
        } else {
            rememberOpenSourceNotices()
        }
        AlertDialog(
            onDismissRequest = { informationDialog = null },
            title = { Text(stringResource(title)) },
            text = {
                Text(
                    body,
                    modifier = Modifier.heightIn(max = 480.dp).verticalScroll(rememberScrollState()),
                )
            },
            confirmButton = {
                TextButton(onClick = { informationDialog = null }) { Text(stringResource(R.string.close)) }
            },
        )
    }
    if (state.confirmEndpointChange) {
        AlertDialog(
            onDismissRequest = { onAction(SettingsAction.CancelEndpointChange) },
            title = { Text(stringResource(R.string.change_console_title)) },
            text = { Text(stringResource(R.string.change_console_body)) },
            dismissButton = {
                TextButton(onClick = { onAction(SettingsAction.CancelEndpointChange) }) {
                    Text(stringResource(R.string.cancel))
                }
            },
            confirmButton = {
                TextButton(onClick = { onAction(SettingsAction.ConfirmEndpointChange) }) {
                    Text(stringResource(R.string.change_console_confirm))
                }
            },
        )
    }
}

@Composable
private fun UpdateControls(state: SettingsUiState, onAction: (SettingsAction) -> Unit) {
    val operationActive = state.updateStatus == UpdateStatus.Checking || state.updateStatus == UpdateStatus.Downloading
    OutlinedButton(
        onClick = { onAction(SettingsAction.CheckUpdate) },
        modifier = Modifier.fillMaxWidth().testTag(UPDATE_CHECK_TEST_TAG),
        enabled = state.profile != null && !operationActive && state.updateStatus != UpdateStatus.Submitted,
    ) {
        Text(stringResource(R.string.check_for_updates))
    }
    when (state.updateStatus) {
        UpdateStatus.Idle -> if (state.profile == null) {
            Text(stringResource(R.string.update_sign_in_required), color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        UpdateStatus.Checking -> UpdateProgress(R.string.checking_for_updates)
        UpdateStatus.Current -> Text(stringResource(R.string.software_is_current), color = MaterialTheme.colorScheme.primary)
        UpdateStatus.Available -> {
            Text(
                stringResource(
                    R.string.update_available_format,
                    state.updateVersion.orEmpty(),
                    state.updateBuildNumber ?: 0,
                ),
            )
            Button(
                onClick = { onAction(SettingsAction.InstallUpdate) },
                modifier = Modifier.fillMaxWidth().testTag(UPDATE_INSTALL_TEST_TAG),
            ) {
                Text(stringResource(R.string.download_and_install))
            }
        }
        UpdateStatus.Downloading -> UpdateProgress(R.string.preparing_update)
        UpdateStatus.Submitted -> Text(
            stringResource(R.string.update_submitted),
            color = MaterialTheme.colorScheme.primary,
        )
        UpdateStatus.Failed -> Text(
            stringResource(state.updateFailure?.labelResource() ?: R.string.error_invalid_response),
            color = MaterialTheme.colorScheme.error,
        )
    }
}

@Composable
private fun UpdateProgress(labelResource: Int) {
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
        CircularProgressIndicator()
        Text(stringResource(labelResource))
    }
}

internal const val UPDATE_CHECK_TEST_TAG = "settings-update-check"
internal const val UPDATE_INSTALL_TEST_TAG = "settings-update-install"

private enum class InformationDialog { Privacy, OpenSource }

@Composable
private fun ConsoleEndpointForm(state: SettingsUiState, onAction: (SettingsAction) -> Unit) {
    if (!state.endpointEditable) {
        Text(stringResource(R.string.official_console_endpoint), color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(state.consoleEndpoint, style = MaterialTheme.typography.bodyLarge)
        return
    }
    OutlinedTextField(
        value = state.consoleEndpoint,
        onValueChange = { onAction(SettingsAction.ConsoleEndpointChanged(it)) },
        modifier = Modifier.fillMaxWidth(),
        label = { Text(stringResource(R.string.console_endpoint)) },
        placeholder = { Text("https://console.example.com") },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
    )
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        OutlinedButton(onClick = { onAction(SettingsAction.TestEndpoint) }, enabled = !state.endpointTesting) {
            if (state.endpointTesting) CircularProgressIndicator() else Text(stringResource(R.string.test_connection))
        }
        Button(onClick = { onAction(SettingsAction.SaveEndpoint) }) { Text(stringResource(R.string.save_endpoint)) }
    }
    if (state.endpointTested) Text(stringResource(R.string.connection_succeeded), color = MaterialTheme.colorScheme.primary)
}

@Composable
private fun LoginForm(state: SettingsUiState, onAction: (SettingsAction) -> Unit) {
    OutlinedTextField(
        value = state.username,
        onValueChange = { onAction(SettingsAction.UsernameChanged(it)) },
        modifier = Modifier.fillMaxWidth(),
        label = { Text(stringResource(R.string.username)) },
        singleLine = true,
    )
    if (state.registrationMode) {
        OutlinedTextField(
            value = state.confirmPassword,
            onValueChange = { onAction(SettingsAction.ConfirmPasswordChanged(it)) },
            modifier = Modifier.fillMaxWidth(),
            label = { Text(stringResource(R.string.confirm_password)) },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
        )
        if (state.confirmPassword.isNotEmpty() && state.password != state.confirmPassword) {
            Text(stringResource(R.string.error_password_mismatch), color = MaterialTheme.colorScheme.error)
        }
    }
    OutlinedTextField(
        value = state.password,
        onValueChange = { onAction(SettingsAction.PasswordChanged(it)) },
        modifier = Modifier.fillMaxWidth(),
        label = { Text(stringResource(R.string.password)) },
        singleLine = true,
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
    )
    state.failure?.let { failure ->
        Text(
            stringResource(failure.labelResource()),
            color = MaterialTheme.colorScheme.error,
            style = MaterialTheme.typography.bodyMedium,
        )
    }
    Button(
        onClick = { onAction(if (state.registrationMode) SettingsAction.Register else SettingsAction.Login) },
        modifier = Modifier.fillMaxWidth(),
        enabled = state.consoleEndpoint.isNotBlank() && state.username.isNotBlank() && state.password.isNotBlank() &&
            (!state.registrationMode || state.password == state.confirmPassword),
    ) {
        Text(stringResource(if (state.registrationMode) R.string.create_account else R.string.sign_in))
    }
    TextButton(onClick = { onAction(SettingsAction.ToggleRegistration) }, modifier = Modifier.fillMaxWidth()) {
        Text(stringResource(if (state.registrationMode) R.string.have_account else R.string.need_account))
    }
}

@Composable
private fun SignedInAccount(state: SettingsUiState, onAction: (SettingsAction) -> Unit) {
    val profile = requireNotNull(state.profile)
    Text(profile.username, style = MaterialTheme.typography.titleMedium)
    Text(state.consoleEndpoint, color = MaterialTheme.colorScheme.onSurfaceVariant)
    if (profile.mustChangePassword) {
        Text(stringResource(R.string.password_change_required), color = MaterialTheme.colorScheme.error)
    }
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        OutlinedButton(onClick = { onAction(SettingsAction.Logout) }) {
            Text(stringResource(R.string.sign_out))
        }
    }
}

private fun AccountFailure.labelResource(): Int = when (this) {
    AccountFailure.InvalidEndpoint -> R.string.error_invalid_endpoint
    AccountFailure.InvalidCredentials -> R.string.error_invalid_credentials
    AccountFailure.AuthenticationRequired -> R.string.error_authentication_required
    AccountFailure.Forbidden -> R.string.error_forbidden
    AccountFailure.RateLimited -> R.string.error_rate_limited
    AccountFailure.DeviceOffline -> R.string.error_device_offline
    AccountFailure.NotFound -> R.string.error_not_found
    AccountFailure.NetworkUnavailable -> R.string.error_network_unavailable
    AccountFailure.InvalidResponse -> R.string.error_invalid_response
    AccountFailure.ServerError -> R.string.error_server
    AccountFailure.UsernameConflict -> R.string.error_username_conflict
    AccountFailure.QuotaExceeded -> R.string.error_quota_exceeded
    AccountFailure.InstanceBusy -> R.string.error_instance_busy
    AccountFailure.UnsupportedApplication -> R.string.error_unsupported_application
    AccountFailure.AccountCreatedLoginFailed -> R.string.error_account_created_login_failed
}
