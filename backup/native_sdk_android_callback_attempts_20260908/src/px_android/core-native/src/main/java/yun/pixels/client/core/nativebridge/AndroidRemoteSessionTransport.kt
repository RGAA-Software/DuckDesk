package yun.pixels.client.core.nativebridge

import android.view.Surface
import java.io.Closeable
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.ConnectionTicket
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

/** Android lifecycle and one-time ticket ownership around the single native SDK. */
class AndroidRemoteSessionTransport private constructor(
    private val native: NativeRemoteSessionTransport,
    private val renewTicket: suspend (ConnectionTicket, String) -> AccountResult<ConnectionTicket>,
) : RemoteSessionTransport by native,
    FileTransferTransport by native,
    RecordingTransport by native,
    VoiceCallTransport by native,
    Closeable {
    constructor(
        installationIdentity: InstallationIdentity,
        callbackScope: CoroutineScope,
        renewTicket: suspend (ConnectionTicket, String) -> AccountResult<ConnectionTicket>,
    ) : this(NativeRemoteSessionTransport(installationIdentity, callbackScope), renewTicket)

    private val lifecycle = Mutex()
    private val activeSessions = mutableSetOf<RemoteSessionId>()
    private val surfaceSessions = mutableSetOf<RemoteSessionId>()
    private val ticketAttempts = mutableMapOf<RemoteSessionId, ConnectionTicketAttempt>()
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
            val account = request.target as? RemoteSessionTarget.Account
            val attempt = account?.let { ticketAttempts.getOrPut(request.id) { ConnectionTicketAttempt(it.connectionTicket) } }
            val effective = if (account != null && attempt != null) {
                when (val prepared = attempt.prepareForStart(System.currentTimeMillis(), account.clientNonce, ::renewTicketSafely)) {
                    is AccountResult.Success -> request.copy(target = account.copy(connectionTicket = prepared.value))
                    is AccountResult.Failure -> return@withLock RemoteTransportStartResult.Rejected(prepared.reason.toSessionFailure())
                }
            } else {
                request
            }
            currentCoroutineContext().ensureActive()
            try {
                // Once JNI creates a handle, finish publishing it before cancellation can interrupt cleanup.
                val result = withContext(NonCancellable) { native.start(effective) }
                currentCoroutineContext().ensureActive()
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
            // Retain the latest rotating renewal capability, not the original request ticket.
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
            native.clearSurfaceBindings()
            activeSessions.clear()
            surfaceSessions.clear()
            ticketAttempts.clear()
        }
    }

    private suspend fun renewTicketSafely(ticket: ConnectionTicket, clientNonce: String): AccountResult<ConnectionTicket> = try {
        renewTicket(ticket, clientNonce)
    } catch (cancellation: CancellationException) {
        throw cancellation
    } catch (_: Exception) {
        AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }
}

private fun AccountFailure.toSessionFailure(): RemoteSessionFailure = when (this) {
    AccountFailure.AuthenticationRequired, AccountFailure.Forbidden, AccountFailure.InvalidCredentials ->
        RemoteSessionFailure.AuthenticationRejected
    AccountFailure.DeviceOffline, AccountFailure.NotFound -> RemoteSessionFailure.DeviceOffline
    AccountFailure.NetworkUnavailable, AccountFailure.RateLimited, AccountFailure.ServerError ->
        RemoteSessionFailure.NetworkUnavailable
    AccountFailure.InvalidEndpoint, AccountFailure.InvalidResponse -> RemoteSessionFailure.ProtocolError
}

/** Serialized by the owning transport lifecycle mutex. Never log either capability. */
internal class ConnectionTicketAttempt(initial: ConnectionTicket) {
    var current: ConnectionTicket = initial
        private set
    private var attempted = false

    fun requiresRenewal(nowEpochMillis: Long): Boolean = attempted || current.expiresAtEpochMillis <= nowEpochMillis + 15_000L

    suspend fun prepareForStart(
        nowEpochMillis: Long,
        clientNonce: String,
        renew: suspend (ConnectionTicket, String) -> AccountResult<ConnectionTicket>,
    ): AccountResult<ConnectionTicket> {
        currentCoroutineContext().ensureActive()
        if (requiresRenewal(nowEpochMillis)) {
            when (val result = renew(current, clientNonce)) {
                is AccountResult.Success -> renewed(result.value)
                is AccountResult.Failure -> return result
            }
        }
        // A non-cooperative renewal may return after cancellation. Retain its rotated capability,
        // but never start JNI or consume the fresh ticket for a cancelled attempt.
        currentCoroutineContext().ensureActive()
        markAttempted()
        return AccountResult.Success(current)
    }

    fun markAttempted() { attempted = true }

    fun renewed(ticket: ConnectionTicket) {
        current = ticket
        attempted = false
    }
}
