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

enum class RemoteInputMode {
    DirectTouch,
    Touchpad,
}

enum class RemoteMouseButton {
    Left,
    Middle,
    Right,
}

enum class RemoteKey(val virtualKeyCode: Int) {
    Backspace(0x08),
    Tab(0x09),
    Enter(0x0D),
    Shift(0x10),
    Control(0x11),
    Alt(0x12),
    Pause(0x13),
    CapsLock(0x14),
    Escape(0x1B),
    Space(0x20),
    PageUp(0x21),
    PageDown(0x22),
    End(0x23),
    Home(0x24),
    ArrowLeft(0x25),
    ArrowUp(0x26),
    ArrowRight(0x27),
    ArrowDown(0x28),
    PrintScreen(0x2C),
    Insert(0x2D),
    Delete(0x2E),
    Digit0(0x30),
    Digit1(0x31),
    Digit2(0x32),
    Digit3(0x33),
    Digit4(0x34),
    Digit5(0x35),
    Digit6(0x36),
    Digit7(0x37),
    Digit8(0x38),
    Digit9(0x39),
    A(0x41),
    B(0x42),
    C(0x43),
    D(0x44),
    E(0x45),
    F(0x46),
    G(0x47),
    H(0x48),
    I(0x49),
    J(0x4A),
    K(0x4B),
    L(0x4C),
    M(0x4D),
    N(0x4E),
    O(0x4F),
    P(0x50),
    Q(0x51),
    R(0x52),
    S(0x53),
    T(0x54),
    U(0x55),
    V(0x56),
    W(0x57),
    X(0x58),
    Y(0x59),
    Z(0x5A),
    Meta(0x5B),
    F1(0x70),
    F2(0x71),
    F3(0x72),
    F4(0x73),
    F5(0x74),
    F6(0x75),
    F7(0x76),
    F8(0x77),
    F9(0x78),
    F10(0x79),
    F11(0x7A),
    F12(0x7B),
    NumLock(0x90),
    ScrollLock(0x91),
    Semicolon(0xBA),
    Equals(0xBB),
    Comma(0xBC),
    Minus(0xBD),
    Period(0xBE),
    Slash(0xBF),
    Grave(0xC0),
    LeftBracket(0xDB),
    Backslash(0xDC),
    RightBracket(0xDD),
    Apostrophe(0xDE),
}

sealed interface InputCommand {
    data class MoveAbsolute(val xRatio: Float, val yRatio: Float) : InputCommand {
        init {
            require(xRatio in 0f..1f && yRatio in 0f..1f) { "Pointer ratios must be normalized" }
        }
    }

    data class MoveRelative(val deltaXRatio: Float, val deltaYRatio: Float) : InputCommand {
        init {
            require(deltaXRatio.isFinite() && deltaYRatio.isFinite()) { "Pointer deltas must be finite" }
        }
    }

    data class MouseButton(
        val button: RemoteMouseButton,
        val down: Boolean,
        val xRatio: Float? = null,
        val yRatio: Float? = null,
    ) : InputCommand {
        init {
            require((xRatio == null) == (yRatio == null)) { "Pointer position must be complete or absent" }
            require(xRatio == null || xRatio in 0f..1f) { "Pointer x ratio must be normalized" }
            require(yRatio == null || yRatio in 0f..1f) { "Pointer y ratio must be normalized" }
        }
    }

    data class Wheel(val deltaX: Int, val deltaY: Int) : InputCommand {
        init {
            require(deltaX != 0 || deltaY != 0) { "Wheel input must contain a non-zero delta" }
        }
    }

    data class Key(val key: RemoteKey, val down: Boolean) : InputCommand

    data class Text(val value: String) : InputCommand {
        init {
            require(value.isNotEmpty() && value.encodeToByteArray().size <= 4096) { "Text input must contain 1 to 4096 UTF-8 bytes" }
        }
    }

    data object SecureAttention : InputCommand
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
