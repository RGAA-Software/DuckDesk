package yun.pixels.client.core.domain.session

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.device.RemoteDevice

@JvmInline
value class RemoteSessionId(val value: String)

sealed interface RemoteSessionTarget {
    val displayName: String

    data class Direct(
        val device: RemoteDevice,
        val credential: String?,
    ) : RemoteSessionTarget {
        override val displayName: String = device.displayName
    }

    data class Account(
        val device: AccountDevice,
        val connectionTicket: ConnectionTicket,
        val clientNonce: String,
    ) : RemoteSessionTarget {
        override val displayName: String = device.displayName
    }
}

data class RemoteSessionRequest(
    val id: RemoteSessionId,
    val target: RemoteSessionTarget,
    val enableVideo: Boolean = true,
    val enableAudio: Boolean = true,
    val enableInput: Boolean = true,
)

data class RemoteSessionCapabilities(
    val monitorNames: List<String>,
    val activeMonitorName: String,
    val supportsAudio: Boolean,
    val supportsInput: Boolean,
    val supportsFileTransfer: Boolean,
    val supportsClipboard: Boolean,
)

data class RemoteSessionStatistics(
    val framesPerSecond: Int = 0,
    val latencyMillis: Int = 0,
    val bitrateKbps: Int = 0,
    val packetLossPercent: Float = 0f,
)

data class RemoteVideoSize(val width: Int, val height: Int) {
    init {
        require(width > 0 && height > 0) { "Remote video dimensions must be positive" }
    }
}

enum class PointerAction {
    Down,
    Move,
    Up,
}

sealed interface InputCommand {
    data class Pointer(val action: PointerAction, val xRatio: Float, val yRatio: Float) : InputCommand {
        init {
            require(xRatio in 0f..1f && yRatio in 0f..1f) { "Pointer ratios must be normalized" }
        }
    }
}

fun interface InstallationIdentity {
    suspend fun value(): String
}

enum class RemoteSessionFailure {
    InvalidRequest,
    AuthenticationRejected,
    DeviceOffline,
    NetworkUnavailable,
    TransportUnavailable,
    DecoderUnavailable,
    ProtocolError,
    RemoteEnded,
}

sealed interface RemoteSessionStatus {
    data object Idle : RemoteSessionStatus

    data class Starting(val request: RemoteSessionRequest) : RemoteSessionStatus

    data class Connected(
        val request: RemoteSessionRequest,
        val capabilities: RemoteSessionCapabilities,
    ) : RemoteSessionStatus

    data class Reconnecting(val request: RemoteSessionRequest, val attempt: Int) : RemoteSessionStatus

    data class Stopping(val request: RemoteSessionRequest) : RemoteSessionStatus

    data class Failed(val request: RemoteSessionRequest, val reason: RemoteSessionFailure) : RemoteSessionStatus
}

data class RemoteSessionSnapshot(
    val status: RemoteSessionStatus = RemoteSessionStatus.Idle,
    val statistics: RemoteSessionStatistics = RemoteSessionStatistics(),
    val videoSize: RemoteVideoSize? = null,
)

sealed interface RemoteTransportStartResult {
    data object Accepted : RemoteTransportStartResult

    data class Rejected(val reason: RemoteSessionFailure) : RemoteTransportStartResult
}

sealed interface RemoteTransportEvent {
    val sessionId: RemoteSessionId

    data class Connected(
        override val sessionId: RemoteSessionId,
        val capabilities: RemoteSessionCapabilities,
    ) : RemoteTransportEvent

    data class Reconnecting(override val sessionId: RemoteSessionId, val attempt: Int) : RemoteTransportEvent

    data class Statistics(override val sessionId: RemoteSessionId, val value: RemoteSessionStatistics) : RemoteTransportEvent

    data class VideoSize(override val sessionId: RemoteSessionId, val value: RemoteVideoSize) : RemoteTransportEvent

    data class Disconnected(
        override val sessionId: RemoteSessionId,
        val reason: RemoteSessionFailure,
        val recoverable: Boolean,
    ) : RemoteTransportEvent
}

interface RemoteSessionTransport {
    val events: Flow<RemoteTransportEvent>

    suspend fun start(request: RemoteSessionRequest): RemoteTransportStartResult

    suspend fun stop(sessionId: RemoteSessionId)

    suspend fun sendInput(sessionId: RemoteSessionId, command: InputCommand): Boolean
}

class RemoteSessionWorkflow(
    private val transport: RemoteSessionTransport,
    scope: CoroutineScope,
) {
    private val commandMutex = Mutex()
    private val stateMutex = Mutex()
    private val mutableSnapshot = MutableStateFlow(RemoteSessionSnapshot())
    private val eventJob: Job = scope.launch(start = CoroutineStart.UNDISPATCHED) { transport.events.collect(::handleTransportEvent) }

    val snapshot: StateFlow<RemoteSessionSnapshot> = mutableSnapshot.asStateFlow()

    suspend fun start(request: RemoteSessionRequest) {
        commandMutex.withLock {
            val activeRequest = stateMutex.withLock {
                currentRequest()?.also { current ->
                    if (current.id == request.id && mutableSnapshot.value.status !is RemoteSessionStatus.Failed) return
                    mutableSnapshot.value = mutableSnapshot.value.copy(status = RemoteSessionStatus.Stopping(current))
                }
            }
            if (activeRequest != null) transport.stop(activeRequest.id)
            stateMutex.withLock { mutableSnapshot.value = RemoteSessionSnapshot(RemoteSessionStatus.Starting(request)) }
            when (val result = transport.start(request)) {
                RemoteTransportStartResult.Accepted -> Unit
                is RemoteTransportStartResult.Rejected -> stateMutex.withLock {
                    if (currentRequest()?.id == request.id) {
                        mutableSnapshot.value = RemoteSessionSnapshot(RemoteSessionStatus.Failed(request, result.reason))
                    }
                }
            }
        }
    }

    suspend fun stop() {
        commandMutex.withLock {
            val request = stateMutex.withLock {
                currentRequest()?.also { mutableSnapshot.value = mutableSnapshot.value.copy(status = RemoteSessionStatus.Stopping(it)) }
            } ?: return
            transport.stop(request.id)
            stateMutex.withLock {
                if (currentRequest()?.id == request.id) mutableSnapshot.value = RemoteSessionSnapshot()
            }
        }
    }

    suspend fun close() {
        stop()
        eventJob.cancel()
    }

    private suspend fun handleTransportEvent(event: RemoteTransportEvent) {
        var terminalSessionId: RemoteSessionId? = null
        stateMutex.withLock {
            val request = currentRequest()?.takeIf { it.id == event.sessionId } ?: return
            if (mutableSnapshot.value.status is RemoteSessionStatus.Stopping || mutableSnapshot.value.status is RemoteSessionStatus.Failed) return
            when (event) {
                is RemoteTransportEvent.Connected -> mutableSnapshot.value = RemoteSessionSnapshot(
                    status = RemoteSessionStatus.Connected(request, event.capabilities),
                )
                is RemoteTransportEvent.Reconnecting -> mutableSnapshot.value = mutableSnapshot.value.copy(
                    status = RemoteSessionStatus.Reconnecting(request, event.attempt),
                )
                is RemoteTransportEvent.Statistics -> mutableSnapshot.value = mutableSnapshot.value.copy(statistics = event.value)
                is RemoteTransportEvent.VideoSize -> mutableSnapshot.value = mutableSnapshot.value.copy(videoSize = event.value)
                is RemoteTransportEvent.Disconnected -> {
                    if (event.recoverable) {
                        mutableSnapshot.value = mutableSnapshot.value.copy(status = RemoteSessionStatus.Reconnecting(request, 1))
                    } else {
                        mutableSnapshot.value = RemoteSessionSnapshot(RemoteSessionStatus.Failed(request, event.reason))
                        terminalSessionId = request.id
                    }
                }
            }
        }
        terminalSessionId?.let { transport.stop(it) }
    }

    private fun currentRequest(): RemoteSessionRequest? = when (val status = mutableSnapshot.value.status) {
        RemoteSessionStatus.Idle -> null
        is RemoteSessionStatus.Starting -> status.request
        is RemoteSessionStatus.Connected -> status.request
        is RemoteSessionStatus.Reconnecting -> status.request
        is RemoteSessionStatus.Stopping -> status.request
        is RemoteSessionStatus.Failed -> status.request
    }
}
