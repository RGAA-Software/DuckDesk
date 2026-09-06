@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package yun.pixels.client.feature.transfer

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import yun.pixels.client.core.domain.transfer.FileTransferDirection
import yun.pixels.client.core.domain.transfer.FileTransferState
import yun.pixels.client.core.domain.transfer.FileTransferTask
import java.util.Locale

@Composable
fun TransferScreen(
    tasks: List<FileTransferTask>,
    sessionConnected: Boolean,
    supportsFileTransfer: Boolean,
    onChooseUpload: (String) -> Unit,
    onChooseDownloadDestination: (String) -> Unit,
    onCancel: (String) -> Unit,
    onRetry: (String) -> Unit,
    onResolveOverwrite: (String, Boolean, Boolean) -> Unit,
    onClearFinished: () -> Unit,
) {
    var uploadDirectory by remember { mutableStateOf("C:/Users/Public/Downloads") }
    var downloadPath by remember { mutableStateOf("") }
    val enabled = sessionConnected && supportsFileTransfer
    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(
            title = { Text(stringResource(R.string.transfers_title), fontWeight = FontWeight.SemiBold) },
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
                OutlinedTextField(
                    value = uploadDirectory,
                    onValueChange = { uploadDirectory = it },
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.remote_upload_directory)) },
                    singleLine = true,
                )
                Button(
                    onClick = { onChooseUpload(uploadDirectory.trim()) },
                    enabled = enabled && uploadDirectory.isNotBlank(),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 8.dp),
                ) { Text(stringResource(R.string.choose_upload_file)) }
            }
            item {
                OutlinedTextField(
                    value = downloadPath,
                    onValueChange = { downloadPath = it },
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.remote_download_path)) },
                    singleLine = true,
                )
                OutlinedButton(
                    onClick = { onChooseDownloadDestination(downloadPath.trim()) },
                    enabled = enabled && downloadPath.isNotBlank(),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 8.dp),
                ) { Text(stringResource(R.string.choose_download_destination)) }
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
