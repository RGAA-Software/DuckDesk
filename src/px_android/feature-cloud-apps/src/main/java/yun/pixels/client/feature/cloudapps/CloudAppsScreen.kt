package yun.pixels.client.feature.cloudapps

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.PlayArrow
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material.icons.outlined.Stop
import androidx.compose.material.icons.outlined.Tune
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationAccess
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import yun.pixels.client.core.domain.account.RemoteApplicationType

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CloudAppsScreen(
    state: CloudAppsUiState,
    onOpenSettings: () -> Unit,
    onRefresh: () -> Unit,
    onStart: (RemoteApplication) -> Unit,
    onConnect: (RemoteApplication) -> Unit,
    onStop: (RemoteApplication) -> Unit,
    onEditPreferences: (RemoteApplication) -> Unit,
) {
    Scaffold(topBar = {
        TopAppBar(
            title = { Text(stringResource(R.string.cloud_apps_title)) },
            actions = {
                if (state.endpointConfigured) {
                    TextButton(onClick = onOpenSettings) {
                        Text(stringResource(if (state.signedIn) R.string.account else R.string.sign_in))
                    }
                }
                IconButton(onClick = onRefresh, enabled = state.endpointConfigured && !state.loading) {
                    Icon(Icons.Outlined.Refresh, stringResource(R.string.refresh))
                }
            },
        )
    }) { padding ->
        when {
            !state.endpointConfigured -> EmptyState(padding = padding, text = stringResource(R.string.configure_console), action = onOpenSettings)
            state.loading && state.applications.isEmpty() -> Column(
                Modifier.fillMaxSize().padding(padding), Arrangement.Center, Alignment.CenterHorizontally,
            ) { CircularProgressIndicator() }
            state.applications.isEmpty() -> EmptyState(
                padding = padding,
                text = stringResource(if (state.failure == null) R.string.no_cloud_apps else state.failure.labelResource()),
                action = onRefresh,
            )
            else -> LazyColumn(
                modifier = Modifier.fillMaxSize().padding(padding).padding(horizontal = 16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                item { state.failure?.let { Text(stringResource(it.labelResource()), color = MaterialTheme.colorScheme.error) } }
                items(state.applications, key = RemoteApplication::appId) { app ->
                    CloudAppCard(app, state.pendingAppId == app.appId, onStart, onConnect, onStop, onEditPreferences)
                }
            }
        }
    }
}

@Composable
private fun EmptyState(padding: androidx.compose.foundation.layout.PaddingValues, text: String, action: () -> Unit) {
    Column(
        modifier = Modifier.fillMaxSize().padding(padding).padding(32.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterVertically),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(text)
        OutlinedButton(onClick = action) { Text(stringResource(R.string.continue_action)) }
    }
}

@Composable
private fun CloudAppCard(
    app: RemoteApplication,
    pending: Boolean,
    onStart: (RemoteApplication) -> Unit,
    onConnect: (RemoteApplication) -> Unit,
    onStop: (RemoteApplication) -> Unit,
    onEditPreferences: (RemoteApplication) -> Unit,
) {
    val runningInstance = app.runningInstance
    Card(Modifier.fillMaxWidth()) {
        Row(
            Modifier.fillMaxWidth().padding(18.dp),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(app.name, style = MaterialTheme.typography.titleMedium)
                Text(
                    "${stringResource(app.type.labelResource())} · ${stringResource(app.access.labelResource())}",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    if (!app.isSupported) stringResource(R.string.unsupported_type)
                    else runningInstance?.let { stringResource(it.state.labelResource()) } ?: stringResource(R.string.ready),
                    color = if (app.isSupported) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.error,
                )
            }
            if (pending) {
                CircularProgressIndicator()
            } else if (app.isSupported) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = { onEditPreferences(app) }) {
                        Icon(Icons.Outlined.Tune, stringResource(R.string.connection_preferences))
                    }
                    if (runningInstance == null) {
                        Button(onClick = { onStart(app) }) {
                            Icon(Icons.Outlined.PlayArrow, null)
                            Text(stringResource(R.string.start))
                        }
                    } else {
                        Column(horizontalAlignment = Alignment.End) {
                            if (runningInstance.reconnectable) {
                                Button(onClick = { onConnect(app) }) { Text(stringResource(R.string.connect)) }
                            }
                            OutlinedButton(onClick = { onStop(app) }) {
                                Icon(Icons.Outlined.Stop, null)
                                Text(stringResource(R.string.stop))
                            }
                        }
                    }
                }
            }
        }
    }
}

private fun RemoteApplicationType.labelResource() = when (this) {
    RemoteApplicationType.GameHook -> R.string.type_game
    RemoteApplicationType.WebView -> R.string.type_web
    RemoteApplicationType.Rdp -> R.string.type_rdp
    RemoteApplicationType.Unknown -> R.string.type_unknown
}

private fun RemoteApplicationAccess.labelResource() = when (this) {
    RemoteApplicationAccess.Public -> R.string.access_public
    RemoteApplicationAccess.Acl -> R.string.access_account
    RemoteApplicationAccess.Unknown -> R.string.access_unknown
}

private fun RemoteApplicationInstance.State.labelResource() = when (this) {
    RemoteApplicationInstance.State.Starting -> R.string.starting
    RemoteApplicationInstance.State.Running -> R.string.running
    RemoteApplicationInstance.State.Stopping -> R.string.stopping
    RemoteApplicationInstance.State.Stopped -> R.string.stopped
    RemoteApplicationInstance.State.Failed -> R.string.failed
}

private fun AccountFailure.labelResource() = when (this) {
    AccountFailure.InvalidEndpoint -> R.string.error_invalid_endpoint
    AccountFailure.UntrustedDeployment -> R.string.error_untrusted_deployment
    AccountFailure.InvalidCredentials -> R.string.error_credentials
    AccountFailure.AuthenticationRequired -> R.string.error_auth
    AccountFailure.Forbidden -> R.string.error_forbidden
    AccountFailure.RateLimited -> R.string.error_rate
    AccountFailure.DeviceOffline -> R.string.error_offline
    AccountFailure.NotFound -> R.string.error_not_found
    AccountFailure.NetworkUnavailable -> R.string.error_network
    AccountFailure.InvalidResponse -> R.string.error_response
    AccountFailure.ServerError -> R.string.error_server
    AccountFailure.UsernameConflict -> R.string.error_conflict
    AccountFailure.QuotaExceeded -> R.string.error_quota
    AccountFailure.InstanceBusy -> R.string.error_busy
    AccountFailure.UnsupportedApplication -> R.string.unsupported_type
    AccountFailure.AccountCreatedLoginFailed -> R.string.error_login
}
