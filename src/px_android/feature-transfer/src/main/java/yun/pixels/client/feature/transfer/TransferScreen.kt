@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package yun.pixels.client.feature.transfer

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.automirrored.outlined.InsertDriveFile
import androidx.compose.material.icons.outlined.Folder
import androidx.compose.material.icons.outlined.Home
import androidx.compose.material.icons.outlined.KeyboardArrowUp
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import yun.pixels.client.core.domain.transfer.FileTransferDirection
import yun.pixels.client.core.domain.transfer.FileTransferState
import yun.pixels.client.core.domain.transfer.FileTransferTask
import yun.pixels.client.core.domain.transfer.RemoteDirectoryState
import yun.pixels.client.core.domain.transfer.RemoteFileEntry
import java.util.Locale

@Composable
fun TransferScreen(
    tasks: List<FileTransferTask>,
    remoteDirectory: RemoteDirectoryState,
    sessionConnected: Boolean,
    supportsFileTransfer: Boolean,
    onBack: (() -> Unit)? = null,
    onBrowseRemoteDirectory: (String) -> Unit,
    onChooseUpload: (String) -> Unit,
    onChooseDownloadDestination: (String) -> Unit,
    onCancel: (String) -> Unit,
    onRetry: (String) -> Unit,
    onResolveOverwrite: (String, Boolean, Boolean) -> Unit,
    onClearFinished: () -> Unit,
) {
    var selectedFilePath by rememberSaveable { mutableStateOf<String?>(null) }
    val enabled = sessionConnected && supportsFileTransfer
    val currentPath = remoteDirectory.pathOrRoot
    val parentPath = remoteParentPath(currentPath)
    LaunchedEffect(currentPath) { selectedFilePath = null }
    BackHandler(enabled = enabled && parentPath != null) { parentPath?.let(onBrowseRemoteDirectory) }
    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(
            title = { Text(stringResource(R.string.transfers_title), fontWeight = FontWeight.SemiBold) },
            navigationIcon = {
                if (onBack != null) {
                    IconButton(onClick = { parentPath?.let(onBrowseRemoteDirectory) ?: onBack() }) {
                        Icon(Icons.AutoMirrored.Outlined.ArrowBack, contentDescription = stringResource(R.string.back))
                    }
                }
            },
            actions = {
                if (tasks.any { it.state.isTerminal }) TextButton(onClick = onClearFinished) { Text(stringResource(R.string.clear_finished)) }
            },
        )
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 20.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item {
                Text(
                    text = when {
                        !sessionConnected -> stringResource(R.string.transfer_requires_session)
                        !supportsFileTransfer -> stringResource(R.string.transfer_not_supported)
                        else -> stringResource(R.string.transfer_ready)
                    },
                    color = if (enabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
            item {
                RemoteDirectoryToolbar(currentPath, enabled, onBrowseRemoteDirectory)
            }
            item {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = { onChooseUpload(currentPath) },
                        enabled = enabled && currentPath != "/" && remoteDirectory is RemoteDirectoryState.Ready,
                        modifier = Modifier.weight(1f),
                    ) { Text(stringResource(R.string.upload_to_current_folder)) }
                    OutlinedButton(
                        onClick = { selectedFilePath?.let(onChooseDownloadDestination) },
                        enabled = enabled && selectedFilePath != null,
                        modifier = Modifier.weight(1f),
                    ) { Text(stringResource(R.string.download_selected_file)) }
                }
            }
            when (remoteDirectory) {
                RemoteDirectoryState.Idle -> item {
                    Text(stringResource(R.string.remote_directory_idle), color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                is RemoteDirectoryState.Loading -> item {
                    Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 2.dp)
                        Text(stringResource(R.string.loading_remote_directory))
                    }
                }
                is RemoteDirectoryState.Failed -> item {
                    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer)) {
                        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(remoteDirectory.reason, color = MaterialTheme.colorScheme.onErrorContainer)
                            TextButton(onClick = { onBrowseRemoteDirectory(remoteDirectory.path) }, enabled = enabled) {
                                Text(stringResource(R.string.retry_transfer))
                            }
                        }
                    }
                }
                is RemoteDirectoryState.Ready -> {
                    if (remoteDirectory.truncated) item {
                        Text(stringResource(R.string.remote_directory_truncated), color = MaterialTheme.colorScheme.tertiary)
                    }
                    if (remoteDirectory.entries.isEmpty()) item {
                        Text(stringResource(R.string.remote_directory_empty), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    itemsIndexed(
                        items = remoteDirectory.entries,
                        key = { index, entry -> "$index:${entry.type}:${entry.name}" },
                    ) { _, entry ->
                        val entryPath = remoteEntryPath(remoteDirectory.path, entry)
                        RemoteFileCard(
                            entry = entry,
                            selected = !entry.isDirectory && selectedFilePath == entryPath,
                            enabled = enabled,
                            onClick = {
                                if (entry.isDirectory) onBrowseRemoteDirectory(entryPath) else selectedFilePath = entryPath
                            },
                        )
                    }
                }
            }
            item {
                Text(
                    stringResource(R.string.transfer_tasks),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                )
            }
            if (tasks.isEmpty()) {
                item { Text(stringResource(R.string.no_transfer_tasks), color = MaterialTheme.colorScheme.onSurfaceVariant) }
            } else {
                items(tasks, key = FileTransferTask::taskId) { task ->
                    TransferTaskCard(task, onCancel, onRetry)
                }
            }
        }
    }
    tasks.firstOrNull { it.state == FileTransferState.AwaitingOverwrite }?.let { task ->
        OverwriteDialog(task, onResolveOverwrite)
    }
}

@Composable
private fun RemoteDirectoryToolbar(path: String, enabled: Boolean, onBrowse: (String) -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.current_remote_folder), style = MaterialTheme.typography.labelLarge)
            Text(path, style = MaterialTheme.typography.bodyMedium)
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                IconButton(onClick = { onBrowse("/") }, enabled = enabled) {
                    Icon(Icons.Outlined.Home, contentDescription = stringResource(R.string.remote_root))
                }
                IconButton(onClick = { remoteParentPath(path)?.let(onBrowse) }, enabled = enabled && remoteParentPath(path) != null) {
                    Icon(Icons.Outlined.KeyboardArrowUp, contentDescription = stringResource(R.string.parent_folder))
                }
                IconButton(onClick = { onBrowse(path) }, enabled = enabled) {
                    Icon(Icons.Outlined.Refresh, contentDescription = stringResource(R.string.refresh_remote_folder))
                }
            }
        }
    }
}

@Composable
private fun RemoteFileCard(entry: RemoteFileEntry, selected: Boolean, enabled: Boolean, onClick: () -> Unit) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = enabled, onClick = onClick),
        colors = CardDefaults.cardColors(
            containerColor = if (selected) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surfaceContainer,
        ),
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                if (entry.isDirectory) Icons.Outlined.Folder else Icons.AutoMirrored.Outlined.InsertDriveFile,
                contentDescription = null,
                tint = if (entry.isDirectory) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Column(modifier = Modifier.weight(1f)) {
                Text(entry.name, style = MaterialTheme.typography.bodyLarge)
                if (!entry.isDirectory) {
                    Text(
                        formatBytes(entry.size),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
    }
}

@Composable
private fun TransferTaskCard(task: FileTransferTask, onCancel: (String) -> Unit, onRetry: (String) -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(task.name, modifier = Modifier.weight(1f), style = MaterialTheme.typography.titleMedium)
                Text(stringResource(task.direction.label), color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.labelLarge)
            }
            val progress = task.progress
            if (task.state in setOf(FileTransferState.Preparing, FileTransferState.Running, FileTransferState.AwaitingOverwrite)) {
                if (progress == null) LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                else LinearProgressIndicator(progress = { progress }, modifier = Modifier.fillMaxWidth())
            }
            Text(
                text = buildString {
                    append(stringResource(task.state.label))
                    if (task.totalBytes > 0) append(" · ${formatBytes(task.completedBytes)} / ${formatBytes(task.totalBytes)}")
                    if (task.speedBytesPerSecond > 0) append(" · ${formatBytes(task.speedBytesPerSecond.toLong())}/s")
                    if (task.fileCount > 1) append(" · ${task.fileNumber + 1}/${task.fileCount}")
                },
                color = if (task.state == FileTransferState.Failed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodySmall,
            )
            if (task.error.isNotBlank()) Text(task.error, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
            when (task.state) {
                FileTransferState.Preparing, FileTransferState.Queued, FileTransferState.Running, FileTransferState.AwaitingOverwrite ->
                    TextButton(onClick = { onCancel(task.taskId) }) { Text(stringResource(R.string.cancel_transfer)) }
                FileTransferState.Failed, FileTransferState.Cancelled ->
                    TextButton(onClick = { onRetry(task.taskId) }) { Text(stringResource(R.string.retry_transfer)) }
                FileTransferState.Succeeded -> Unit
            }
        }
    }
}

@Composable
private fun OverwriteDialog(task: FileTransferTask, onResolve: (String, Boolean, Boolean) -> Unit) {
    var applyToAll by remember(task.taskId, task.fileNumber) { mutableStateOf(false) }
    AlertDialog(
        onDismissRequest = {},
        title = { Text(stringResource(R.string.overwrite_title)) },
        text = {
            Column {
                Text(stringResource(R.string.overwrite_body, task.name))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(checked = applyToAll, onCheckedChange = { applyToAll = it })
                    Text(stringResource(R.string.apply_to_all))
                }
            }
        },
        confirmButton = { TextButton(onClick = { onResolve(task.taskId, true, applyToAll) }) { Text(stringResource(R.string.overwrite)) } },
        dismissButton = { TextButton(onClick = { onResolve(task.taskId, false, applyToAll) }) { Text(stringResource(R.string.skip)) } },
    )
}

internal val FileTransferTask.progress: Float?
    get() = totalBytes.takeIf { it > 0 }?.let { (completedBytes.toDouble() / it.toDouble()).coerceIn(0.0, 1.0).toFloat() }

private val FileTransferState.isTerminal: Boolean
    get() = this in setOf(FileTransferState.Succeeded, FileTransferState.Failed, FileTransferState.Cancelled)

private val FileTransferDirection.label: Int
    get() = if (this == FileTransferDirection.Upload) R.string.upload else R.string.download

private val FileTransferState.label: Int
    get() = when (this) {
        FileTransferState.Preparing -> R.string.preparing
        FileTransferState.Queued -> R.string.queued
        FileTransferState.Running -> R.string.running
        FileTransferState.AwaitingOverwrite -> R.string.awaiting_overwrite
        FileTransferState.Succeeded -> R.string.succeeded
        FileTransferState.Failed -> R.string.failed
        FileTransferState.Cancelled -> R.string.cancelled
    }

private fun formatBytes(value: Long): String {
    if (value < 1024) return "$value B"
    val units = arrayOf("KiB", "MiB", "GiB", "TiB")
    var amount = value.toDouble()
    var unit = -1
    do {
        amount /= 1024.0
        unit++
    } while (amount >= 1024.0 && unit < units.lastIndex)
    return String.format(Locale.getDefault(), "%.1f %s", amount, units[unit])
}

private val RemoteDirectoryState.pathOrRoot: String
    get() = when (this) {
        RemoteDirectoryState.Idle -> "/"
        is RemoteDirectoryState.Loading -> path
        is RemoteDirectoryState.Ready -> path
        is RemoteDirectoryState.Failed -> path
    }

internal fun remoteEntryPath(directory: String, entry: RemoteFileEntry): String {
    if (entry.absolutePath.isNotBlank()) return normalizeRemotePath(entry.absolutePath)
    val base = normalizeRemotePath(directory)
    return normalizeRemotePath(if (base == "/") "/${entry.name}" else "$base/${entry.name}")
}

internal fun remoteParentPath(path: String): String? {
    val normalized = normalizeRemotePath(path)
    if (normalized == "/") return null
    if (normalized.length == 3 && normalized[1] == ':' && normalized.endsWith('/')) return "/"
    val parent = normalized.substringBeforeLast('/', missingDelimiterValue = "")
    return when {
        parent.isEmpty() -> "/"
        parent.length == 2 && parent[1] == ':' -> "$parent/"
        else -> parent
    }
}

private fun normalizeRemotePath(path: String): String {
    val normalized = path.trim().replace('\\', '/').ifBlank { "/" }
    if (normalized == "/") return normalized
    val withoutTrailingSlash = normalized.trimEnd('/')
    return if (withoutTrailingSlash.length == 2 && withoutTrailingSlash[1] == ':') "$withoutTrailingSlash/" else withoutTrailingSlash
}
