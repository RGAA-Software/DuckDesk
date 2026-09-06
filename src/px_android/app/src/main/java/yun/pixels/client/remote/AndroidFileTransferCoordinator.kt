package yun.pixels.client.remote

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import yun.pixels.client.R
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.transfer.FileTransferDirection
import yun.pixels.client.core.domain.transfer.FileTransferEvent
import yun.pixels.client.core.domain.transfer.FileTransferState
import yun.pixels.client.core.domain.transfer.FileTransferTask
import yun.pixels.client.core.domain.transfer.FileTransferTransport
import yun.pixels.client.core.domain.transfer.RemoteDirectoryEvent
import yun.pixels.client.core.domain.transfer.RemoteDirectoryState
import java.io.File
import java.util.UUID

class AndroidFileTransferCoordinator(
    context: Context,
    private val transport: FileTransferTransport,
    private val scope: CoroutineScope,
) {
    private val appContext = context.applicationContext
    private val resolver: ContentResolver = context.contentResolver
    private val stagingRoot = File(context.cacheDir, "file-transfers")
    private val mutableTasks = MutableStateFlow<List<FileTransferTask>>(emptyList())
    private val mutableRemoteDirectory = MutableStateFlow<RemoteDirectoryState>(RemoteDirectoryState.Idle)
    private val activeTasks = mutableMapOf<String, ActiveTask>()
    private val taskIdsByJob = mutableMapOf<JobKey, String>()
    private val pendingEvents = mutableMapOf<JobKey, MutableList<FileTransferEvent>>()

    val tasks: StateFlow<List<FileTransferTask>> = mutableTasks.asStateFlow()
    val remoteDirectory: StateFlow<RemoteDirectoryState> = mutableRemoteDirectory.asStateFlow()

    private var directorySessionId: RemoteSessionId? = null
    private var directoryRequestGeneration = 0L

    init {
        scope.launch { transport.fileTransferEvents.collect(::handleEvent) }
        scope.launch { transport.remoteDirectoryEvents.collect(::handleDirectoryEvent) }
    }

    fun browse(sessionId: RemoteSessionId, path: String) {
        val normalizedPath = normalizeRemotePath(path)
        if (normalizedPath.length > MAX_REMOTE_PATH_LENGTH || normalizedPath.any(Char::isISOControl)) {
            mutableRemoteDirectory.value = RemoteDirectoryState.Failed(
                normalizedPath,
                appContext.getString(R.string.transfer_error_invalid_remote_path),
            )
            return
        }
        directorySessionId = sessionId
        val generation = ++directoryRequestGeneration
        mutableRemoteDirectory.value = RemoteDirectoryState.Loading(normalizedPath)
        scope.launch {
            if (!transport.listRemoteDirectory(sessionId, normalizedPath)) {
                if (generation == directoryRequestGeneration) {
                    mutableRemoteDirectory.value = RemoteDirectoryState.Failed(
                        normalizedPath,
                        appContext.getString(R.string.transfer_error_directory_request),
                    )
                }
                return@launch
            }
            delay(REMOTE_DIRECTORY_TIMEOUT_MILLIS)
            if (generation == directoryRequestGeneration && mutableRemoteDirectory.value is RemoteDirectoryState.Loading) {
                mutableRemoteDirectory.value = RemoteDirectoryState.Failed(
                    normalizedPath,
                    appContext.getString(R.string.transfer_error_directory_timeout),
                )
            }
        }
    }

    fun upload(sessionId: RemoteSessionId, source: Uri, remoteDirectory: String) {
        val taskId = UUID.randomUUID().toString()
        val name = runCatching { resolver.displayName(source) }.getOrDefault("pixels-transfer").sanitizeFileName()
        val root = File(stagingRoot, taskId)
        val sourceFile = File(root, name)
        val active = ActiveTask(
            taskId = taskId,
            sessionId = sessionId,
            direction = FileTransferDirection.Upload,
            name = name,
            stagingDirectory = root,
            localSource = sourceFile,
            remotePath = remoteDestinationPath(remoteDirectory.trim(), name),
        )
        activeTasks[taskId] = active
        upsert(active.toTask())
        scope.launch {
            val staged = withContext(Dispatchers.IO) {
                runCatching {
                    root.mkdirs()
                    resolver.openInputStream(source).use { input ->
                        requireNotNull(input) { appContext.getString(R.string.transfer_error_read_source) }
                        sourceFile.outputStream().use(input::copyTo)
                    }
                }
            }
            if (staged.isFailure) {
                fail(active, staged.exceptionOrNull()?.message ?: appContext.getString(R.string.transfer_error_stage))
                return@launch
            }
            if (active.cancelRequested) {
                cleanupStaging(active)
                return@launch
            }
            start(active)
        }
    }

    fun download(sessionId: RemoteSessionId, remotePath: String, destination: Uri) {
        val taskId = UUID.randomUUID().toString()
        val name = remotePath.substringAfterLast('/').substringAfterLast('\\').ifBlank { resolver.displayName(destination) }.sanitizeFileName()
        val active = ActiveTask(
            taskId = taskId,
            sessionId = sessionId,
            direction = FileTransferDirection.Download,
            name = name,
            stagingDirectory = File(stagingRoot, taskId),
            remotePath = remotePath.trim(),
            destination = destination,
        )
        active.stagingDirectory.mkdirs()
        activeTasks[taskId] = active
        upsert(active.toTask())
        scope.launch { start(active) }
    }

    fun cancel(taskId: String) {
        val active = activeTasks[taskId] ?: return
        active.cancelRequested = true
        val jobId = active.nativeJobId
        if (jobId == null) {
            complete(active, FileTransferState.Cancelled)
            return
        }
        scope.launch {
            if (!transport.cancelTransfer(active.sessionId, jobId)) {
                active.cancelRequested = false
                upsert(active.toTask(state = FileTransferState.Running, error = appContext.getString(R.string.transfer_error_cancel)))
            }
        }
    }

    fun retry(taskId: String) {
        val active = activeTasks[taskId] ?: return
        val state = mutableTasks.value.firstOrNull { it.taskId == taskId }?.state ?: return
        if (state !in setOf(FileTransferState.Failed, FileTransferState.Cancelled)) return
        active.nativeJobId?.let { taskIdsByJob.remove(JobKey(active.sessionId, it)) }
        active.nativeJobId = null
        active.cancelRequested = false
        active.running = false
        if (active.direction == FileTransferDirection.Download) {
            cleanupStaging(active)
            active.stagingDirectory.mkdirs()
        }
        upsert(active.toTask())
        scope.launch { start(active) }
    }

    fun resolveOverwrite(taskId: String, overwrite: Boolean, applyToAll: Boolean) {
        val active = activeTasks[taskId] ?: return
        val jobId = active.nativeJobId ?: return
        val fileNumber = active.awaitingFileNumber ?: return
        scope.launch {
            val accepted = transport.confirmOverwrite(active.sessionId, jobId, fileNumber, overwrite, applyToAll = applyToAll)
            if (accepted) {
                active.awaitingFileNumber = null
                upsert(active.toTask(state = FileTransferState.Running))
            } else {
                upsert(active.toTask(state = FileTransferState.AwaitingOverwrite, error = appContext.getString(R.string.transfer_error_confirm)))
            }
        }
    }

    fun clearFinished() {
        val removable = mutableTasks.value.filter { it.state in TERMINAL_STATES }.mapTo(mutableSetOf(), FileTransferTask::taskId)
        removable.forEach { taskId ->
            activeTasks.remove(taskId)?.let { active ->
                active.nativeJobId?.let { taskIdsByJob.remove(JobKey(active.sessionId, it)) }
                cleanupStaging(active)
            }
        }
        mutableTasks.value = mutableTasks.value.filterNot { it.taskId in removable }
    }

    fun sessionEnded(sessionId: RemoteSessionId) {
        activeTasks.values.filter { it.sessionId == sessionId }.forEach { active ->
            val task = mutableTasks.value.firstOrNull { it.taskId == active.taskId } ?: return@forEach
            if (task.state !in TERMINAL_STATES) {
                active.cancelRequested = true
                active.running = false
                upsert(active.toTask(state = FileTransferState.Cancelled, error = appContext.getString(R.string.transfer_error_session_ended)))
            }
        }
        if (directorySessionId == sessionId) {
            directorySessionId = null
            directoryRequestGeneration++
            mutableRemoteDirectory.value = RemoteDirectoryState.Idle
        }
    }

    private fun handleDirectoryEvent(event: RemoteDirectoryEvent) {
        if (event.sessionId != directorySessionId) return
        val loading = mutableRemoteDirectory.value as? RemoteDirectoryState.Loading ?: return
        if (!sameRemotePath(loading.path, event.path)) return
        directoryRequestGeneration++
        mutableRemoteDirectory.value = RemoteDirectoryState.Ready(
            path = normalizeRemotePath(event.path),
            entries = event.entries.sortedWith(compareByDescending<yun.pixels.client.core.domain.transfer.RemoteFileEntry> { it.isDirectory }
                .thenBy(String.CASE_INSENSITIVE_ORDER) { it.name }),
            truncated = event.truncated,
        )
    }

    private suspend fun start(active: ActiveTask) {
        active.scheduled = false
        if (active.cancelRequested) return
        if (activeTasks.values.count { it.running || it.scheduled } >= MAX_CONCURRENT_TRANSFERS) {
            upsert(active.toTask(state = FileTransferState.Queued, error = ""))
            return
        }
        active.running = true
        upsert(active.toTask(state = FileTransferState.Preparing, error = ""))
        val jobId = when (active.direction) {
            FileTransferDirection.Upload -> transport.startUpload(active.sessionId, requireNotNull(active.localSource).absolutePath, active.remotePath)
            FileTransferDirection.Download -> transport.startDownload(
                active.sessionId,
                active.remotePath,
                File(active.stagingDirectory, active.name).absolutePath,
            )
        }
        if (jobId == null) {
            fail(active, appContext.getString(R.string.transfer_error_not_started))
            return
        }
        active.nativeJobId = jobId
        val key = JobKey(active.sessionId, jobId)
        taskIdsByJob[key] = active.taskId
        upsert(active.toTask(state = FileTransferState.Running))
        pendingEvents.remove(key)?.forEach { event -> handleEvent(event) }
    }

    private suspend fun handleEvent(event: FileTransferEvent) {
        val key = JobKey(event.sessionId, event.jobId)
        val taskId = taskIdsByJob[key]
        if (taskId == null) {
            if (pendingEvents.size < MAX_PENDING_JOB_COUNT) pendingEvents.getOrPut(key, ::mutableListOf).add(event)
            return
        }
        val active = activeTasks[taskId] ?: return
        when (event) {
            is FileTransferEvent.Progress -> upsert(
                active.toTask(
                    state = if (active.awaitingFileNumber == null) FileTransferState.Running else FileTransferState.AwaitingOverwrite,
                    fileNumber = event.fileNumber,
                    fileCount = event.fileCount,
                    totalBytes = event.totalBytes,
                    completedBytes = event.completedBytes,
                    speedBytesPerSecond = event.speedBytesPerSecond,
                ),
            )
            is FileTransferEvent.OverwriteRequired -> {
                active.awaitingFileNumber = event.fileNumber
                upsert(active.toTask(state = FileTransferState.AwaitingOverwrite))
            }
            is FileTransferEvent.Completed -> handleCompleted(active, event.error)
        }
    }

    private suspend fun handleCompleted(active: ActiveTask, error: String) {
        if (active.cancelRequested) {
            complete(active, FileTransferState.Cancelled)
            return
        }
        if (error.isNotBlank()) {
            fail(active, error)
            return
        }
        if (active.direction == FileTransferDirection.Download) {
            val published = withContext(Dispatchers.IO) { publishDownload(active) }
            if (published.isFailure) {
                fail(active, published.exceptionOrNull()?.message ?: appContext.getString(R.string.transfer_error_publish))
                return
            }
        }
        complete(active, FileTransferState.Succeeded)
    }

    private fun publishDownload(active: ActiveTask): Result<Unit> = runCatching {
        val destination = requireNotNull(active.destination)
        val downloadedFiles = active.stagingDirectory.walkTopDown().filter(File::isFile).take(2).toList()
        require(downloadedFiles.size == 1) {
            appContext.getString(if (downloadedFiles.isEmpty()) R.string.transfer_error_no_file else R.string.transfer_error_not_single_file)
        }
        val source = downloadedFiles.single()
        resolver.openOutputStream(destination, "wt").use { output ->
            requireNotNull(output) { appContext.getString(R.string.transfer_error_write_destination) }
            source.inputStream().use { input -> input.copyTo(output) }
        }
    }

    private fun complete(active: ActiveTask, state: FileTransferState) {
        active.running = false
        upsert(active.toTask(state = state, completedBytes = if (state == FileTransferState.Succeeded) active.lastTotalBytes else active.lastCompletedBytes))
        if (state == FileTransferState.Succeeded) cleanupStaging(active)
        startNextQueued()
    }

    private fun fail(active: ActiveTask, error: String) {
        active.running = false
        upsert(active.toTask(state = FileTransferState.Failed, error = error.ifBlank { appContext.getString(R.string.transfer_error_generic) }))
        startNextQueued()
    }

    private fun startNextQueued() {
        val available = MAX_CONCURRENT_TRANSFERS - activeTasks.values.count { it.running || it.scheduled }
        if (available <= 0) return
        mutableTasks.value.asSequence()
            .filter { it.state == FileTransferState.Queued }
            .take(available)
            .mapNotNull { activeTasks[it.taskId] }
            .forEach { active ->
                active.scheduled = true
                scope.launch { start(active) }
            }
    }

    private fun cleanupStaging(active: ActiveTask) {
        active.stagingDirectory.deleteRecursively()
        File("${active.stagingDirectory.absolutePath}.download").delete()
        File("${active.stagingDirectory.absolutePath}.digest").delete()
    }

    private fun upsert(task: FileTransferTask) {
        val current = mutableTasks.value
        val existing = current.indexOfFirst { it.taskId == task.taskId }
        mutableTasks.value = if (existing < 0) listOf(task) + current else current.toMutableList().also { it[existing] = task }
        activeTasks[task.taskId]?.apply {
            lastFileNumber = task.fileNumber
            lastFileCount = task.fileCount
            lastTotalBytes = task.totalBytes
            lastCompletedBytes = task.completedBytes
            lastSpeed = task.speedBytesPerSecond
            lastError = task.error
        }
    }

    private data class JobKey(val sessionId: RemoteSessionId, val jobId: Int)

    private data class ActiveTask(
        val taskId: String,
        val sessionId: RemoteSessionId,
        val direction: FileTransferDirection,
        val name: String,
        val stagingDirectory: File,
        val remotePath: String,
        val localSource: File? = null,
        val destination: Uri? = null,
        var nativeJobId: Int? = null,
        var awaitingFileNumber: Int? = null,
        var cancelRequested: Boolean = false,
        var running: Boolean = false,
        var scheduled: Boolean = false,
        var lastFileNumber: Int = 0,
        var lastFileCount: Int = 0,
        var lastTotalBytes: Long = 0,
        var lastCompletedBytes: Long = 0,
        var lastSpeed: Double = 0.0,
        var lastError: String = "",
    ) {
        fun toTask(
            state: FileTransferState = FileTransferState.Preparing,
            fileNumber: Int = lastFileNumber,
            fileCount: Int = lastFileCount,
            totalBytes: Long = lastTotalBytes,
            completedBytes: Long = lastCompletedBytes,
            speedBytesPerSecond: Double = lastSpeed,
            error: String = lastError,
        ) = FileTransferTask(
            taskId = taskId,
            sessionId = sessionId,
            nativeJobId = nativeJobId,
            name = name,
            direction = direction,
            state = state,
            fileNumber = fileNumber,
            fileCount = fileCount,
            totalBytes = totalBytes,
            completedBytes = completedBytes,
            speedBytesPerSecond = speedBytesPerSecond,
            error = error,
        )
    }

    private fun ContentResolver.displayName(uri: Uri): String = query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
        if (cursor.moveToFirst()) cursor.getString(0) else null
    }.orEmpty().ifBlank { "pixels-transfer" }

    private fun String.sanitizeFileName(): String = replace(Regex("[\\\\/:*?\"<>|\\p{Cntrl}]"), "_").take(180).ifBlank { "pixels-transfer" }

    private companion object {
        val TERMINAL_STATES = setOf(FileTransferState.Succeeded, FileTransferState.Failed, FileTransferState.Cancelled)
        const val MAX_PENDING_JOB_COUNT = 32
        const val MAX_CONCURRENT_TRANSFERS = 2
        const val MAX_REMOTE_PATH_LENGTH = 4096
        const val REMOTE_DIRECTORY_TIMEOUT_MILLIS = 8_000L
    }
}

internal fun remoteDestinationPath(directory: String, fileName: String): String = "${directory.trimEnd('/', '\\')}/$fileName"

internal fun normalizeRemotePath(path: String): String {
    val normalized = path.trim().replace('\\', '/').ifBlank { "/" }
    if (normalized == "/") return normalized
    val withoutTrailingSlash = normalized.trimEnd('/')
    return if (withoutTrailingSlash.length == 2 && withoutTrailingSlash[1] == ':') "$withoutTrailingSlash/" else withoutTrailingSlash
}

private fun sameRemotePath(requested: String, received: String): Boolean =
    normalizeRemotePath(requested).equals(normalizeRemotePath(received), ignoreCase = true)
