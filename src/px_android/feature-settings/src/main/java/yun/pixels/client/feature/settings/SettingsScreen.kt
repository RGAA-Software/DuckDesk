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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import yun.pixels.client.core.domain.account.AccountFailure

@Composable
fun SettingsScreen(
    state: SettingsUiState,
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
            Text(stringResource(R.string.account_title), style = MaterialTheme.typography.titleLarge)
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
            Text(stringResource(R.string.about_title), style = MaterialTheme.typography.titleMedium)
            if (appVersion.isNotBlank()) {
                Text(
                    stringResource(R.string.version_format, appVersion),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
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
        val body = if (dialog == InformationDialog.Privacy) R.string.privacy_body else R.string.open_source_body
        AlertDialog(
            onDismissRequest = { informationDialog = null },
            title = { Text(stringResource(title)) },
            text = {
                Text(
                    stringResource(body),
                    modifier = Modifier.heightIn(max = 480.dp).verticalScroll(rememberScrollState()),
                )
            },
            confirmButton = {
                TextButton(onClick = { informationDialog = null }) { Text(stringResource(R.string.close)) }
            },
        )
    }
}

private enum class InformationDialog { Privacy, OpenSource }

@Composable
private fun LoginForm(state: SettingsUiState, onAction: (SettingsAction) -> Unit) {
    OutlinedTextField(
        value = state.consoleEndpoint,
        onValueChange = { onAction(SettingsAction.ConsoleEndpointChanged(it)) },
        modifier = Modifier.fillMaxWidth(),
        label = { Text(stringResource(R.string.console_endpoint)) },
        placeholder = { Text("https://console.example.com") },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
    )
    OutlinedTextField(
        value = state.username,
        onValueChange = { onAction(SettingsAction.UsernameChanged(it)) },
        modifier = Modifier.fillMaxWidth(),
        label = { Text(stringResource(R.string.username)) },
        singleLine = true,
    )
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
        onClick = { onAction(SettingsAction.Login) },
        modifier = Modifier.fillMaxWidth(),
        enabled = state.consoleEndpoint.isNotBlank() && state.username.isNotBlank() && state.password.isNotBlank(),
    ) {
        Text(stringResource(R.string.sign_in))
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
}
