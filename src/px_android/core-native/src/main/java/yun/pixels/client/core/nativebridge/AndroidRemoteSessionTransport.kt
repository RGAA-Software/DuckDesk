package yun.pixels.client.core.nativebridge

import android.view.Surface
import java.io.Closeable
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import yun.pixels.client.core.domain.session.InstallationIdentity
import yun.pixels.client.core.domain.session.RemoteSessionFailure
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionTarget
import yun.pixels.client.core.domain.session.RemoteSessionTransport
import yun.pixels.client.core.domain.session.RemoteTransportStartResult
import yun.pixels.client.core.domain.transfer.FileTransferTransport
import yun.pixels.client.core.domain.recording.RecordingTransport
import yun.pixels.client.core.domain.voice.VoiceCallTransport

/** Android lifecycle ownership around the single native SDK. */
class AndroidRemoteSessionTransport private constructor(
    private val native: NativeRemoteSessionTransport,
) : RemoteSessionTransport by native,
    FileTransferTransport by native,
    RecordingTransport by native,
    VoiceCallTransport by native,
    Closeable {
    constructor(
        installationIdentity: InstallationIdentity,
        callbackScope: CoroutineScope,
    ) : this(NativeRemoteSessionTransport(installationIdentity, callbackScope))

    private val lifecycle = Mutex()
    private val activeSessions = mutableSetOf<RemoteSessionId>()
    private val surfaceSessions = mutableSetOf<RemoteSessionId>()
    private var closed = false

    suspend fun attachSurface(sessionId: RemoteSessionId, surface: Surface) = withContext(Dispatchers.IO) {
        lifecycle.withLock {
            if (!closed) {
                native.attachSurface(sessionId, surface)
                surfaceSessions += sessionId
            }
        }
    }

    suspend fun detachSurface(sessionId: RemoteSessionId, surface: Surface) = withContext(Dispatchers.IO) {
        lifecycle.withLock { native.detachSurface(sessionId, surface) }
    }

    override suspend fun start(request: RemoteSessionRequest): RemoteTransportStartResult = withContext(Dispatchers.IO) {
        lifecycle.withLock {
            if (closed) return@withLock RemoteTransportStartResult.Rejected(RemoteSessionFailure.TransportUnavailable)
            if (request.id in activeSessions) return@withLock RemoteTransportStartResult.Accepted
            try {
                // Once JNI creates a handle, finish publishing it before cancellation can interrupt cleanup.
                val result = withContext(NonCancellable) { native.start(request) }
                if (result == RemoteTransportStartResult.Accepted) activeSessions += request.id
                result
            } catch (cancellation: CancellationException) {
                withContext(NonCancellable) { native.stop(request.id) }
                throw cancellation
            } catch (_: Exception) {
                withContext(NonCancellable) { native.stop(request.id) }
                RemoteTransportStartResult.Rejected(RemoteSessionFailure.TransportUnavailable)
            }
        }
    }

    override suspend fun stop(sessionId: RemoteSessionId) = withContext(Dispatchers.IO) {
        lifecycle.withLock {
            activeSessions -= sessionId
            native.stop(sessionId)
        }
    }

    suspend fun switchMonitor(sessionId: RemoteSessionId, monitorName: String): Boolean = native.switchMonitor(sessionId, monitorName)
    suspend fun setFrameRate(sessionId: RemoteSessionId, frameRate: Int): Boolean = native.setFrameRate(sessionId, frameRate)
    suspend fun setAudioEnabled(sessionId: RemoteSessionId, enabled: Boolean): Boolean = native.setAudioEnabled(sessionId, enabled)

    override fun close() = runBlocking(Dispatchers.IO) {
        lifecycle.withLock {
            if (closed) return@withLock
            closed = true
            (activeSessions + surfaceSessions).forEach { native.stop(it) }
            native.close()
            activeSessions.clear()
            surfaceSessions.clear()
        }
    }
}
