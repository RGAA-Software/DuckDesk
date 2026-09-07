package yun.pixels.client.remote

import android.content.ContentValues
import android.content.Context
import android.os.SystemClock
import android.provider.MediaStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import yun.pixels.client.core.domain.recording.RecordingEvent
import yun.pixels.client.core.domain.recording.RecordingId
import yun.pixels.client.core.domain.recording.RecordingState
import yun.pixels.client.core.domain.recording.RecordingTransport
import yun.pixels.client.core.domain.session.RemoteSessionId
import java.io.File
import java.util.UUID

class AndroidRecordingCoordinator(
    private val context: Context,
    private val transport: RecordingTransport,
    private val scope: CoroutineScope,
) {
    private data class ActiveRecording(
        val sessionId: RemoteSessionId,
        val id: RecordingId,
        val directory: File,
        val startedAtMillis: Long,
    )

    private val rootDirectory = File(context.cacheDir, "recordings")
    private val mutableState = MutableStateFlow<RecordingState>(RecordingState.Idle)
    private var active: ActiveRecording? = null
    private var timerJob: Job? = null
    private val eventJob = scope.launch(start = CoroutineStart.UNDISPATCHED) { transport.recordingEvents.collect(::handleEvent) }

    val state: StateFlow<RecordingState> = mutableState.asStateFlow()

    init {
        scope.launch(Dispatchers.IO) { cleanupStaleDirectories() }
    }

    fun start(sessionId: RemoteSessionId) {
        if (active != null) return
        scope.launch {
            val id = RecordingId(UUID.randomUUID().toString())
            val directory = File(rootDirectory, id.value)
            val ready = withContext(Dispatchers.IO) { directory.mkdirs() || directory.isDirectory }
            if (!ready) {
                mutableState.value = RecordingState.Failed(id, "recording_storage_unavailable")
                return@launch
            }
            val pending = ActiveRecording(sessionId, id, directory, SystemClock.elapsedRealtime())
            active = pending
            mutableState.value = RecordingState.Starting(id)
            if (!transport.startRecording(sessionId, id, directory.absolutePath)) {
                active = null
                mutableState.value = RecordingState.Failed(id, "recording_start_rejected")
                withContext(Dispatchers.IO) { deleteStagingDirectory(directory) }
            }
        }
    }

    fun stop() {
        val current = active ?: return
        if (mutableState.value is RecordingState.Stopping || mutableState.value is RecordingState.Publishing) return
        mutableState.value = RecordingState.Stopping(current.id, elapsedMillis(current))
        timerJob?.cancel()
        timerJob = null
        scope.launch {
            if (!transport.stopRecording(current.sessionId, current.id)) {
                fail(current, "recording_stop_rejected")
            }
        }
    }

    suspend fun sessionEnded(sessionId: RemoteSessionId) {
        if (active?.sessionId != sessionId) return
        stop()
        withTimeoutOrNull(5_000) {
            state.filter { value ->
                value is RecordingState.Completed || value is RecordingState.Failed || value is RecordingState.Idle
            }.first()
        }
        // NativeSession::Stop performs a final synchronous drain after this bounded wait.
        // Keep the staging directory alive so a late writer callback can still publish it.
    }

    fun close() {
        timerJob?.cancel()
        eventJob.cancel()
    }

    private suspend fun handleEvent(event: RecordingEvent) {
        val current = active?.takeIf { it.sessionId == event.sessionId && it.id == event.recordingId } ?: return
        when (event) {
            is RecordingEvent.Started -> {
                mutableState.value = RecordingState.Recording(current.id, elapsedMillis(current))
                timerJob?.cancel()
                timerJob = scope.launch {
                    while (active?.id == current.id) {
                        mutableState.value = when (mutableState.value) {
                            is RecordingState.Recording -> RecordingState.Recording(current.id, elapsedMillis(current))
                            else -> break
                        }
                        delay(1_000)
                    }
                }
            }
            is RecordingEvent.Finished -> {
                timerJob?.cancel()
                timerJob = null
                if (event.error.isNotBlank()) {
                    fail(current, event.error)
                    return
                }
                mutableState.value = RecordingState.Publishing(current.id)
                val result = runCatching { withContext(Dispatchers.IO) { publish(current.directory) } }
                active = null
                mutableState.value = result.fold(
                    onSuccess = { count -> RecordingState.Completed(current.id, count) },
                    onFailure = { error -> RecordingState.Failed(current.id, error.message.orEmpty().ifBlank { "recording_publish_failed" }) },
                )
                withContext(Dispatchers.IO) { deleteStagingDirectory(current.directory) }
            }
        }
    }

    private suspend fun fail(current: ActiveRecording, reason: String) {
        if (active?.id != current.id) return
        timerJob?.cancel()
        timerJob = null
        active = null
        mutableState.value = RecordingState.Failed(current.id, reason)
        withContext(Dispatchers.IO) { deleteStagingDirectory(current.directory) }
    }

    private fun publish(directory: File): Int {
        val files = directory.listFiles()
            ?.filter { file -> file.isFile && file.extension.equals("mp4", ignoreCase = true) && file.length() > 0L }
            ?.sortedBy(File::getName)
            .orEmpty()
        check(files.isNotEmpty()) { "recording_contains_no_video" }
        files.forEach(::publishFile)
        return files.size
    }

    private fun publishFile(source: File) {
        val resolver = context.contentResolver
        val values = ContentValues().apply {
            put(MediaStore.Video.Media.DISPLAY_NAME, source.name)
            put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
            put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/Pixels")
            put(MediaStore.Video.Media.IS_PENDING, 1)
        }
        val destination = checkNotNull(resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)) {
            "recording_media_store_insert_failed"
        }
        try {
            checkNotNull(resolver.openOutputStream(destination, "w")) { "recording_media_store_open_failed" }.use { output ->
                source.inputStream().buffered().use { input -> input.copyTo(output) }
            }
            values.clear()
            values.put(MediaStore.Video.Media.IS_PENDING, 0)
            check(resolver.update(destination, values, null, null) == 1) { "recording_media_store_publish_failed" }
        } catch (error: Throwable) {
            resolver.delete(destination, null, null)
            throw error
        }
    }

    private fun cleanupStaleDirectories() {
        val cutoff = System.currentTimeMillis() - STALE_DIRECTORY_AGE_MILLIS
        rootDirectory.listFiles()?.filter { it.isDirectory && it.lastModified() < cutoff }?.forEach(::deleteStagingDirectory)
    }

    private fun deleteStagingDirectory(directory: File) {
        if (directory.parentFile?.canonicalFile == rootDirectory.canonicalFile) {
            directory.deleteRecursively()
        }
    }

    private fun elapsedMillis(current: ActiveRecording): Long =
        (SystemClock.elapsedRealtime() - current.startedAtMillis).coerceAtLeast(0L)

    private companion object {
        const val STALE_DIRECTORY_AGE_MILLIS = 24L * 60 * 60 * 1_000
    }
}
