package yun.pixels.client.core.nativebridge

import android.content.Context
import android.view.Surface
import java.io.Closeable
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.suspendCancellableCoroutine
import okhttp3.OkHttpClient
import com.google.protobuf.ByteString
import org.webrtc.DataChannel
import org.webrtc.DefaultVideoDecoderFactory
import org.webrtc.DefaultVideoEncoderFactory
import org.webrtc.EglBase
import org.webrtc.GlRectDrawer
import org.webrtc.IceCandidate
import org.webrtc.MediaConstraints
import org.webrtc.MediaStream
import org.webrtc.MediaStreamTrack
import org.webrtc.PeerConnection
import org.webrtc.PeerConnectionFactory
import org.webrtc.RendererCommon
import org.webrtc.RtpReceiver
import org.webrtc.RtpTransceiver
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import org.webrtc.SurfaceEglRenderer
import org.webrtc.VideoTrack
import px.PxMessage
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.RemoteMouseButton

internal class WebRtcRuntime(context: Context) : Closeable {
    private val closed = AtomicBoolean(false)
    private val eglBase: EglBase
    val eglContext: EglBase.Context
    val factory: PeerConnectionFactory

    init {
        initializeWebRtc(context.applicationContext)
        eglBase = EglBase.create()
        eglContext = eglBase.eglBaseContext
        factory = PeerConnectionFactory.builder()
            .setVideoEncoderFactory(DefaultVideoEncoderFactory(eglContext, true, true))
            .setVideoDecoderFactory(DefaultVideoDecoderFactory(eglContext))
            .createPeerConnectionFactory()
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        factory.dispose()
        eglBase.release()
    }

    private companion object {
        val initialized = AtomicBoolean(false)

        fun initializeWebRtc(context: Context) {
            if (initialized.get()) return
            synchronized(initialized) {
                if (initialized.get()) return
                PeerConnectionFactory.initialize(
                    PeerConnectionFactory.InitializationOptions.builder(context)
                        .setEnableInternalTracer(false)
                        .createInitializationOptions(),
                )
                initialized.set(true)
            }
        }
    }
}

internal sealed interface WebRtcPeerEvent {
    data object Connected : WebRtcPeerEvent

    data object Reconnecting : WebRtcPeerEvent

    data class VideoSize(val width: Int, val height: Int) : WebRtcPeerEvent

    data class ServerConfiguration(val value: px.PxMessage.ServerConfiguration) : WebRtcPeerEvent

    data class ClipboardText(val value: String) : WebRtcPeerEvent

    data class GamepadRumble(val strongMotor: Int, val weakMotor: Int) : WebRtcPeerEvent

    data class Closed(val reason: String, val recoverable: Boolean) : WebRtcPeerEvent
}

internal class WebRtcPeerSession(
    private val runtime: WebRtcRuntime,
    private val httpClient: OkHttpClient,
    private val scope: CoroutineScope,
    private val parameters: StandardRtcSignalParameters,
    private val iceConfiguration: RtcIceConfiguration,
    surface: Surface,
    private val enableVideo: Boolean,
    private val enableAudio: Boolean,
    private val enableInput: Boolean,
    private val onEvent: (WebRtcPeerEvent) -> Unit,
) : Closeable {
    private val stateLock = Any()
    private val closed = AtomicBoolean(false)
    private val renderer = SurfaceEglRenderer("PixelsAndroidRtc")
    private var peerConnection: PeerConnection? = null
    private var signaling: StandardRtcSignaling? = null
    private var mediaChannel: DataChannel? = null
    private var inputChannel: DataChannel? = null
    private var videoTrack: VideoTrack? = null
    private var renderSurface: Surface? = surface
    private var packetIndex = 0L
    private var activeMonitorName = ""
    private var cursorX = 0.5f
    private var cursorY = 0.5f
    private val pendingRemoteIce = ArrayDeque<IceCandidate>()

    suspend fun start() {
        check(!closed.get()) { "RTC peer session is closed" }
        renderer.init(runtime.eglContext, Renderer(), EglBase.CONFIG_PLAIN, GlRectDrawer())
        renderer.createEglSurface(requireNotNull(synchronized(stateLock) { renderSurface }))

        val rtcConfiguration = PeerConnection.RTCConfiguration(iceConfiguration.toWebRtcServers()).apply {
            sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
            continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_CONTINUALLY
            iceCandidatePoolSize = 2
        }
        val connection = runtime.factory.createPeerConnection(rtcConfiguration, Observer())
            ?: throw IllegalStateException("WebRTC PeerConnection creation failed")
        synchronized(stateLock) { peerConnection = connection }

        if (enableVideo) {
            connection.addTransceiver(
                MediaStreamTrack.MediaType.MEDIA_TYPE_VIDEO,
                RtpTransceiver.RtpTransceiverInit(RtpTransceiver.RtpTransceiverDirection.RECV_ONLY),
            )
        }
        if (enableAudio) {
            connection.addTransceiver(
                MediaStreamTrack.MediaType.MEDIA_TYPE_AUDIO,
                RtpTransceiver.RtpTransceiverInit(RtpTransceiver.RtpTransceiverDirection.RECV_ONLY),
            )
        }
        mediaChannel = connection.createDataChannel(MEDIA_CHANNEL_LABEL, DataChannel.Init()).also {
            it.registerObserver(ChannelObserver(it, reliableControl = true))
        }
        if (enableInput) {
            inputChannel = connection.createDataChannel(
                INPUT_CHANNEL_LABEL,
                DataChannel.Init().apply {
                    ordered = false
                    maxRetransmits = 0
                },
            ).also { it.registerObserver(ChannelObserver(it, reliableControl = false)) }
        }

        val activeSignaling = StandardRtcSignaling(httpClient, scope, parameters, ::onSignalEvent)
        synchronized(stateLock) { signaling = activeSignaling }
        try {
            activeSignaling.connect()
            val offer = connection.createOfferAwait()
            connection.setLocalDescriptionAwait(offer)
            val answer = activeSignaling.exchangeOffer(offer.description)
            connection.setRemoteDescriptionAwait(SessionDescription(SessionDescription.Type.ANSWER, answer))
            val candidates = synchronized(stateLock) { pendingRemoteIce.toList().also { pendingRemoteIce.clear() } }
            candidates.forEach(connection::addIceCandidate)
        } catch (failure: Throwable) {
            closeWithReason(
                failure.message.orEmpty().ifBlank { "WebRTC negotiation failed" },
                recoverable = true,
                notify = false,
            )
            throw failure
        }
    }

    fun setAudioEnabled(enabled: Boolean): Boolean = synchronized(stateLock) {
        peerConnection?.setAudioPlayout(enabled) ?: return false
        true
    }

    fun replaceSurface(surface: Surface) {
        synchronized(stateLock) { renderSurface = surface }
        renderer.releaseEglSurface {
            val nextSurface = synchronized(stateLock) { renderSurface.takeUnless { closed.get() } }
            if (nextSurface != null) renderer.createEglSurface(nextSurface)
        }
    }

    fun detachSurface(surface: Surface) {
        val shouldDetach = synchronized(stateLock) {
            if (renderSurface !== surface) false else {
                renderSurface = null
                true
            }
        }
        if (shouldDetach) renderer.releaseEglSurface { }
    }

    fun sendInput(command: InputCommand): Boolean = when (command) {
        is InputCommand.MoveAbsolute -> sendMouseMove(command.xRatio, command.yRatio)
        is InputCommand.MoveRelative -> {
            val position = synchronized(stateLock) {
                cursorX = (cursorX + command.deltaXRatio).coerceIn(0f, 1f)
                cursorY = (cursorY + command.deltaYRatio).coerceIn(0f, 1f)
                cursorX to cursorY
            }
            sendMouseMove(position.first, position.second)
        }

        is InputCommand.MouseButton -> {
            val position = synchronized(stateLock) {
                if (command.xRatio != null && command.yRatio != null) {
                    cursorX = command.xRatio ?: cursorX
                    cursorY = command.yRatio ?: cursorY
                }
                cursorX to cursorY
            }
            sendMouse(
                button = command.button.toRtcButtonFlag(command.down),
                xRatio = position.first,
                yRatio = position.second,
                data = 0,
                pressed = command.down,
                released = !command.down,
            )
        }

        is InputCommand.Wheel -> {
            val position = synchronized(stateLock) { cursorX to cursorY }
            val vertical = command.deltaY == 0 || sendMouse(
                button = RTC_MOUSE_WHEEL,
                xRatio = position.first,
                yRatio = position.second,
                data = command.deltaY,
            )
            val horizontal = command.deltaX == 0 || sendMouse(
                button = RTC_MOUSE_HORIZONTAL_WHEEL,
                xRatio = position.first,
                yRatio = position.second,
                data = command.deltaX,
            )
            vertical && horizontal
        }

        is InputCommand.Key -> sendInputMessage(
            PxMessage.Message.newBuilder()
                .setType(PxMessage.MessageType.kKeyEvent)
                .setKeyEvent(
                    PxMessage.KeyEvent.newBuilder()
                        .setKeyCode(command.key.virtualKeyCode)
                        .setDown(command.down)
                        .setNumLockStatus(-1)
                        .setCapsLockStatus(-1)
                        .setStatusCheck(PxMessage.KeyEvent.LockKeyStatusCheck.kDontCareLockKey)
                        .setTimestamp(System.currentTimeMillis()),
                ),
        )

        is InputCommand.Gamepad -> command.state.let { state ->
            sendInputMessage(
                PxMessage.Message.newBuilder()
                    .setType(PxMessage.MessageType.kGamepadState)
                    .setGamepadState(
                        PxMessage.GamepadState.newBuilder()
                            .setButtons(state.buttons)
                            .setLeftTrigger(state.leftTrigger)
                            .setRightTrigger(state.rightTrigger)
                            .setThumbLx(state.leftThumbX)
                            .setThumbLy(state.leftThumbY)
                            .setThumbRx(state.rightThumbX)
                            .setThumbRy(state.rightThumbY),
                    ),
            )
        }

        is InputCommand.Text -> sendInputMessage(
            PxMessage.Message.newBuilder()
                .setType(PxMessage.MessageType.kTextInput)
                .setTextInput(PxMessage.TextInput.newBuilder().setText(command.value)),
        )

        InputCommand.SecureAttention -> sendMediaMessage(
            PxMessage.Message.newBuilder()
                .setType(PxMessage.MessageType.kReqCtrlAltDelete)
                .setReqCtrlAltDelete(PxMessage.ReqCtrlAltDelete.getDefaultInstance()),
        )
    }

    fun sendClipboardText(value: String): Boolean {
        val bytes = value.encodeToByteArray()
        if (bytes.isEmpty() || bytes.size > MAX_CLIPBOARD_TEXT_BYTES) return false
        return sendMediaMessage(
            PxMessage.Message.newBuilder()
                .setType(PxMessage.MessageType.kClipboardInfo)
                .setClipboardInfo(
                    PxMessage.ClipboardInfo.newBuilder()
                        .setType(PxMessage.ClipboardType.kClipboardText)
                        .setMsg(ByteString.copyFrom(bytes)),
                ),
        )
    }

    fun switchMonitor(monitorName: String): Boolean {
        if (monitorName.isBlank()) return false
        return sendMediaMessage(
            PxMessage.Message.newBuilder()
                .setType(PxMessage.MessageType.kSwitchMonitor)
                .setSwitchMonitor(PxMessage.SwitchMonitor.newBuilder().setName(monitorName)),
        )
    }

    override fun close() {
        closeWithReason("RTC peer session stopped", recoverable = false, notify = false)
    }

    private fun onSignalEvent(event: StandardRtcSignalEvent) {
        when (event) {
            is StandardRtcSignalEvent.RemoteIce -> synchronized(stateLock) {
                val connection = peerConnection ?: return
                if (connection.remoteDescription == null) pendingRemoteIce.addLast(event.candidate)
                else connection.addIceCandidate(event.candidate)
            }

            is StandardRtcSignalEvent.Closed -> closeWithReason(event.reason, recoverable = true)
        }
    }

    private fun onTrack(receiver: RtpReceiver) {
        val track = receiver.track() as? VideoTrack ?: return
        synchronized(stateLock) {
            videoTrack?.removeSink(renderer)
            videoTrack = track
            track.addSink(renderer)
        }
    }

    private fun sendHello(channel: DataChannel) {
        val payload = buildRtcHello(
            deviceId = parameters.ticketDeviceId,
            streamId = parameters.streamId,
            enableVideo = enableVideo,
            enableAudio = enableAudio,
            enableInput = enableInput,
        )
        val packet = synchronized(stateLock) { packRtcTlv(payload, packetIndex++) }
        channel.send(DataChannel.Buffer(ByteBuffer.wrap(packet), true))
    }

    private fun onMediaMessage(buffer: DataChannel.Buffer) {
        if (!buffer.binary || buffer.data.remaining() > MAX_CHANNEL_MESSAGE_BYTES) return
        val bytes = ByteArray(buffer.data.remaining())
        buffer.data.get(bytes)
        val payload = unpackRtcTlv(bytes) ?: return
        val message = runCatching { PxMessage.Message.parseFrom(payload) }.getOrNull() ?: return
        when (message.type) {
            PxMessage.MessageType.kServerConfiguration -> {
                synchronized(stateLock) { activeMonitorName = message.config.capturingMonitorName }
                onEvent(WebRtcPeerEvent.ServerConfiguration(message.config))
            }

            PxMessage.MessageType.kMonitorSwitched -> {
                synchronized(stateLock) { activeMonitorName = message.monitorSwitched.name }
            }

            PxMessage.MessageType.kClipboardInfo -> {
                if (message.clipboardInfo.type == PxMessage.ClipboardType.kClipboardText) {
                    runCatching { message.clipboardInfo.msg.toByteArray().decodeToString(throwOnInvalidSequence = true) }
                        .getOrNull()
                        ?.let { onEvent(WebRtcPeerEvent.ClipboardText(it)) }
                }
            }

            PxMessage.MessageType.kGamepadRumble -> onEvent(
                WebRtcPeerEvent.GamepadRumble(
                    strongMotor = message.gamepadRumble.strongMotor.coerceIn(0, 255),
                    weakMotor = message.gamepadRumble.weakMotor.coerceIn(0, 255),
                ),
            )

            else -> Unit
        }
    }

    private fun sendMouseMove(xRatio: Float, yRatio: Float): Boolean {
        synchronized(stateLock) {
            cursorX = xRatio
            cursorY = yRatio
        }
        return sendMouse(RTC_MOUSE_MOVE, xRatio, yRatio, 0)
    }

    private fun sendMouse(
        button: Int,
        xRatio: Float,
        yRatio: Float,
        data: Int,
        pressed: Boolean = false,
        released: Boolean = false,
    ): Boolean {
        val monitorName = synchronized(stateLock) { activeMonitorName }
        if (monitorName.isBlank()) return false
        return sendInputMessage(
            PxMessage.Message.newBuilder()
                .setType(PxMessage.MessageType.kMouseEvent)
                .setMouseEvent(
                    PxMessage.MouseEvent.newBuilder()
                        .setMonitorName(monitorName)
                        .setXRatio(xRatio)
                        .setYRatio(yRatio)
                        .setButton(button)
                        .setData(data)
                        .setPressed(pressed)
                        .setReleased(released)
                        .setTimestamp(System.currentTimeMillis()),
                ),
        )
    }

    private fun sendInputMessage(builder: PxMessage.Message.Builder): Boolean = sendMessage(inputChannel, builder)

    private fun sendMediaMessage(builder: PxMessage.Message.Builder): Boolean = sendMessage(mediaChannel, builder)

    private fun sendMessage(channel: DataChannel?, builder: PxMessage.Message.Builder): Boolean {
        val activeChannel = synchronized(stateLock) { channel?.takeIf { it.state() == DataChannel.State.OPEN } } ?: return false
        val payload = builder
            .setDeviceId(parameters.ticketDeviceId)
            .setStreamId(parameters.streamId)
            .build()
            .toByteArray()
        val packet = synchronized(stateLock) { packRtcTlv(payload, packetIndex++) }
        return activeChannel.send(DataChannel.Buffer(ByteBuffer.wrap(packet), true))
    }

    private fun closeWithReason(reason: String, recoverable: Boolean, notify: Boolean = true) {
        if (!closed.compareAndSet(false, true)) return
        val activeTrack: VideoTrack?
        val activeMedia: DataChannel?
        val activeInput: DataChannel?
        val activeConnection: PeerConnection?
        val activeSignaling: StandardRtcSignaling?
        synchronized(stateLock) {
            activeTrack = videoTrack
            videoTrack = null
            activeMedia = mediaChannel
            mediaChannel = null
            activeInput = inputChannel
            inputChannel = null
            activeConnection = peerConnection
            peerConnection = null
            activeSignaling = signaling
            signaling = null
            pendingRemoteIce.clear()
            renderSurface = null
        }
        activeTrack?.removeSink(renderer)
        activeMedia?.unregisterObserver()
        activeMedia?.close()
        activeMedia?.dispose()
        activeInput?.unregisterObserver()
        activeInput?.close()
        activeInput?.dispose()
        activeSignaling?.close()
        activeConnection?.close()
        activeConnection?.dispose()
        renderer.release()
        if (notify) onEvent(WebRtcPeerEvent.Closed(reason, recoverable))
    }

    private inner class Renderer : RendererCommon.RendererEvents {
        override fun onFirstFrameRendered() = Unit

        override fun onFrameResolutionChanged(videoWidth: Int, videoHeight: Int, rotation: Int) {
            val rotated = rotation % 180 != 0
            onEvent(
                WebRtcPeerEvent.VideoSize(
                    width = if (rotated) videoHeight else videoWidth,
                    height = if (rotated) videoWidth else videoHeight,
                ),
            )
        }
    }

    private inner class ChannelObserver(
        private val channel: DataChannel,
        private val reliableControl: Boolean,
    ) : DataChannel.Observer {
        override fun onBufferedAmountChange(previousAmount: Long) = Unit

        override fun onStateChange() {
            if (reliableControl && channel.state() == DataChannel.State.OPEN) sendHello(channel)
        }

        override fun onMessage(buffer: DataChannel.Buffer) {
            if (reliableControl) onMediaMessage(buffer)
        }
    }

    private inner class Observer : PeerConnection.Observer {
        override fun onConnectionChange(newState: PeerConnection.PeerConnectionState) {
            when (newState) {
                PeerConnection.PeerConnectionState.CONNECTED -> onEvent(WebRtcPeerEvent.Connected)
                PeerConnection.PeerConnectionState.DISCONNECTED -> onEvent(WebRtcPeerEvent.Reconnecting)
                PeerConnection.PeerConnectionState.FAILED -> closeWithReason("WebRTC connection failed", recoverable = true)
                PeerConnection.PeerConnectionState.CLOSED -> closeWithReason("WebRTC connection closed", recoverable = false)
                else -> Unit
            }
        }

        override fun onIceCandidate(candidate: IceCandidate) {
            synchronized(stateLock) { signaling }?.sendIce(candidate)
        }

        override fun onTrack(transceiver: RtpTransceiver) = onTrack(transceiver.receiver)

        override fun onAddTrack(receiver: RtpReceiver, mediaStreams: Array<out MediaStream>) = onTrack(receiver)

        override fun onSignalingChange(newState: PeerConnection.SignalingState) = Unit

        override fun onIceConnectionChange(newState: PeerConnection.IceConnectionState) = Unit

        override fun onIceConnectionReceivingChange(receiving: Boolean) = Unit

        override fun onIceGatheringChange(newState: PeerConnection.IceGatheringState) = Unit

        override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>) = Unit

        override fun onAddStream(stream: MediaStream) = Unit

        override fun onRemoveStream(stream: MediaStream) = Unit

        override fun onDataChannel(channel: DataChannel) = Unit

        override fun onRenegotiationNeeded() = Unit
    }
}

private suspend fun PeerConnection.createOfferAwait(): SessionDescription = suspendCancellableCoroutine { continuation ->
    createOffer(
        object : SdpObserver {
            override fun onCreateSuccess(description: SessionDescription) {
                if (continuation.isActive) continuation.resume(description)
            }

            override fun onCreateFailure(reason: String) {
                if (continuation.isActive) continuation.resumeWithException(IllegalStateException(reason))
            }

            override fun onSetSuccess() = Unit

            override fun onSetFailure(reason: String) = Unit
        },
        MediaConstraints(),
    )
}

private suspend fun PeerConnection.setLocalDescriptionAwait(description: SessionDescription) = setDescriptionAwait(description, true)

private suspend fun PeerConnection.setRemoteDescriptionAwait(description: SessionDescription) = setDescriptionAwait(description, false)

private suspend fun PeerConnection.setDescriptionAwait(description: SessionDescription, local: Boolean) =
    suspendCancellableCoroutine { continuation ->
        val observer = object : SdpObserver {
            override fun onSetSuccess() {
                if (continuation.isActive) continuation.resume(Unit)
            }

            override fun onSetFailure(reason: String) {
                if (continuation.isActive) continuation.resumeWithException(IllegalStateException(reason))
            }

            override fun onCreateSuccess(description: SessionDescription) = Unit

            override fun onCreateFailure(reason: String) = Unit
        }
        if (local) setLocalDescription(observer, description) else setRemoteDescription(observer, description)
    }

private const val MEDIA_CHANNEL_LABEL = "media_data_channel"
private const val INPUT_CHANNEL_LABEL = "input_data_channel"
private const val MAX_CHANNEL_MESSAGE_BYTES = 4 * 1024 * 1024
private const val MAX_CLIPBOARD_TEXT_BYTES = 1024 * 1024
private const val RTC_MOUSE_MOVE = 128
private const val RTC_MOUSE_WHEEL = 256
private const val RTC_MOUSE_HORIZONTAL_WHEEL = 512
private const val RTC_MOUSE_LEFT_UP = 16
private const val RTC_MOUSE_MIDDLE_UP = 32
private const val RTC_MOUSE_RIGHT_UP = 64
private const val RTC_MOUSE_LEFT_DOWN = 1024
private const val RTC_MOUSE_MIDDLE_DOWN = 2048
private const val RTC_MOUSE_RIGHT_DOWN = 4096

private fun RemoteMouseButton.toRtcButtonFlag(down: Boolean): Int = when (this) {
    RemoteMouseButton.Left -> if (down) RTC_MOUSE_LEFT_DOWN else RTC_MOUSE_LEFT_UP
    RemoteMouseButton.Middle -> if (down) RTC_MOUSE_MIDDLE_DOWN else RTC_MOUSE_MIDDLE_UP
    RemoteMouseButton.Right -> if (down) RTC_MOUSE_RIGHT_DOWN else RTC_MOUSE_RIGHT_UP
}
