package yun.pixels.client.feature.devices

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.outlined.PlayArrow
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material.icons.outlined.Stop
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
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationInstance

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ApplicationLibraryScreen(
    state: ApplicationLibraryUiState,
    onBack: () -> Unit,
    onRefresh: () -> Unit,
    onStart: (RemoteApplication) -> Unit,
    onStop: (RemoteApplication) -> Unit,
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.applications_title)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Outlined.ArrowBack, contentDescription = stringResource(R.string.back))
                    }
                },
                actions = {
                    IconButton(onClick = onRefresh, enabled = !state.loading && state.pendingAppId == null) {
                        Icon(Icons.Outlined.Refresh, contentDescription = stringResource(R.string.refresh_applications))
                    }
                },
            )
        },
    ) { padding ->
        when {
            state.loading && state.applications.isEmpty() -> Column(
                modifier = Modifier.fillMaxSize().padding(padding),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) { CircularProgressIndicator() }
            state.failure != null && state.applications.isEmpty() -> Column(
                modifier = Modifier.fillMaxSize().padding(padding).padding(32.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterVertically),
            ) {
                Text(stringResource(state.failure.labelResource()), color = MaterialTheme.colorScheme.error)
                OutlinedButton(onClick = onRefresh) { Text(stringResource(R.string.retry)) }
            }
            state.applications.isEmpty() -> Column(
                modifier = Modifier.fillMaxSize().padding(padding).padding(32.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) { Text(stringResource(R.string.no_applications)) }
            else -> LazyColumn(
                modifier = Modifier.fillMaxSize().padding(padding).padding(horizontal = 16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                item { state.failure?.let { Text(stringResource(it.labelResource()), color = MaterialTheme.colorScheme.error) } }
                items(state.applications, key = RemoteApplication::appId) { application ->
                    ApplicationCard(application, state.pendingAppId == application.appId, onStart, onStop)
                }
            }
        }
    }
}

@Composable
private fun ApplicationCard(
    application: RemoteApplication,
    pending: Boolean,
    onStart: (RemoteApplication) -> Unit,
    onStop: (RemoteApplication) -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(18.dp),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(application.name, style = MaterialTheme.typography.titleMedium)
                Text(
                    application.runningInstance?.state?.label() ?: stringResource(R.string.application_ready),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            if (pending) {
                CircularProgressIndicator(modifier = Modifier.padding(10.dp))
            } else if (application.runningInstance == null) {
                Button(onClick = { onStart(application) }) {
                    Icon(Icons.Outlined.PlayArrow, contentDescription = null)
                    Text(stringResource(R.string.start_application))
                }
            } else {
                OutlinedButton(onClick = { onStop(application) }) {
                    Icon(Icons.Outlined.Stop, contentDescription = null)
                    Text(stringResource(R.string.stop_application))
                }
            }
        }
    }
}

@Composable
private fun RemoteApplicationInstance.State.label(): String = stringResource(
    when (this) {
        RemoteApplicationInstance.State.Starting -> R.string.application_starting
        RemoteApplicationInstance.State.Running -> R.string.application_running
        RemoteApplicationInstance.State.Stopping -> R.string.application_stopping
        RemoteApplicationInstance.State.Stopped -> R.string.application_stopped
        RemoteApplicationInstance.State.Failed -> R.string.application_failed
    },
)

private fun AccountFailure.labelResource(): Int = when (this) {
    AccountFailure.InvalidEndpoint -> R.string.account_error_invalid_endpoint
    AccountFailure.InvalidCredentials -> R.string.account_error_invalid_credentials
    AccountFailure.AuthenticationRequired -> R.string.account_error_authentication_required
    AccountFailure.Forbidden -> R.string.account_error_forbidden
    AccountFailure.RateLimited -> R.string.account_error_rate_limited
    AccountFailure.DeviceOffline -> R.string.account_error_device_offline
    AccountFailure.NotFound -> R.string.account_error_not_found
    AccountFailure.NetworkUnavailable -> R.string.account_error_network
    AccountFailure.InvalidResponse -> R.string.account_error_invalid_response
    AccountFailure.ServerError -> R.string.account_error_server
}
