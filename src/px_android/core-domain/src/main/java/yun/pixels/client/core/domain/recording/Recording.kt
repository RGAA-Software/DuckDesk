package yun.pixels.client.core.domain.recording

import kotlinx.coroutines.flow.Flow
import yun.pixels.client.core.domain.session.RemoteSessionId

@JvmInline
value class RecordingId(val value: String) {
    init {
        require(value.isNotBlank())
        require(value.length <= 128)
    }
}

sealed interface RecordingState {
    data object Idle : RecordingState

    data class Starting(val id: RecordingId) : RecordingState

    data class Recording(val id: RecordingId, val elapsedMillis: Long) : RecordingState

    data class Stopping(val id: RecordingId, val elapsedMillis: Long) : RecordingState

    data class Publishing(val id: RecordingId) : RecordingState

    data class Completed(val id: RecordingId, val itemCount: Int) : RecordingState

    data class Failed(val id: RecordingId, val reason: String) : RecordingState
}

sealed interface RecordingEvent {
    val sessionId: RemoteSessionId
    val recordingId: RecordingId

    data class Started(
        override val sessionId: RemoteSessionId,
        override val recordingId: RecordingId,
    ) : RecordingEvent

    data class Finished(
        override val sessionId: RemoteSessionId,
        override val recordingId: RecordingId,
        val error: String,
    ) : RecordingEvent
}

interface RecordingTransport {
    val recordingEvents: Flow<RecordingEvent>

    suspend fun startRecording(sessionId: RemoteSessionId, recordingId: RecordingId, stagingDirectory: String): Boolean

    suspend fun stopRecording(sessionId: RemoteSessionId, recordingId: RecordingId): Boolean
}
