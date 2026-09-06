package yun.pixels.client.core.nativebridge

import com.google.protobuf.ByteString
import java.io.Closeable
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout
import okhttp3.HttpUrl
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString as OkioByteString
import org.webrtc.IceCandidate
import px.PxMessage
import px.PxSignalingMessage
import px_relay.RelayMessageOuterClass

internal data class StandardRtcSignalParameters(
    val relayHost: String,
    val relayPort: Int,
    val secure: Boolean,
    val remoteDeviceId: String,
    val ticketDeviceId: String,
    val streamId: String,
    val ticket: String,
    val clientNonce: String,
    val instanceId: String,
) {
    init {
        require(relayHost.isNotBlank())
        require(relayPort in 1..65535)
        require(remoteDeviceId.isNotBlank())
        require(ticketDeviceId.isNotBlank())
        require(streamId.isNotBlank())
        require(ticket.isNotBlank())
        require(clientNonce.isNotBlank())
    }
}

internal sealed interface StandardRtcSignalEvent {
    data class RemoteIce(val candidate: IceCandidate) : StandardRtcSignalEvent

    data class Closed(val reason: String) : StandardRtcSignalEvent
}

internal class StandardRtcSignaling(
    private val httpClient: OkHttpClient,
    private val scope: CoroutineScope,
    private val parameters: StandardRtcSignalParameters,
    private val onEvent: (StandardRtcSignalEvent) -> Unit,
    private val clientId: String = "web_${UUID.randomUUID().toString().replace("-", "")}",
) : Closeable {
    private val stateLock = Any()
    private val closed = AtomicBoolean(false)
    private val roomReady = CompletableDeferred<Unit>()
    private var socket: WebSocket? = null
    private var roomId = ""
    private var relayIndex = 0L
    private var heartbeatIndex = 0L
    private var heartbeatJob: Job? = null
    private var pendingAnswer: CompletableDeferred<String>? = null
    private val pendingLocalIce = ArrayDeque<IceCandidate>()
    private var canSendIce = false

    suspend fun connect() {
        check(!closed.get()) { "RTC signaling is closed" }
        val request = Request.Builder().url(buildRequestUrl(parameters, clientId)).build()
        synchronized(stateLock) {
            check(socket == null) { "RTC signaling is already connected" }
            socket = httpClient.newWebSocket(request, Listener())
        }
        withTimeout(CONNECT_TIMEOUT_MILLIS) { roomReady.await() }
    }

    suspend fun exchangeOffer(sdp: String): String {
        require(sdp.isNotBlank())
        val answer = synchronized(stateLock) {
            check(roomId.isNotBlank()) { "RTC signaling room is not ready" }
            check(pendingAnswer == null) { "RTC offer exchange is already active" }
            CompletableDeferred<String>().also {
                pendingAnswer = it
                canSendIce = true
                sendPxMessageLocked(buildOffer(sdp))
                while (pendingLocalIce.isNotEmpty()) sendIceLocked(pendingLocalIce.removeFirst())
            }
        }
        return try {
            withTimeout(ANSWER_TIMEOUT_MILLIS) { answer.await() }
        } finally {
            synchronized(stateLock) {
                if (pendingAnswer === answer) pendingAnswer = null
            }
        }
    }

    fun sendIce(candidate: IceCandidate) {
        synchronized(stateLock) {
            if (closed.get()) return
            if (roomId.isBlank() || !canSendIce) {
                if (pendingLocalIce.size == MAX_PENDING_ICE) pendingLocalIce.removeFirst()
                pendingLocalIce.addLast(candidate)
                return
            }
            sendIceLocked(candidate)
        }
    }

    override fun close() {
        fail("RTC signaling stopped", report = false)
    }

    internal fun requestUrl(): HttpUrl = buildRequestUrl(parameters, clientId)

    private fun onSocketOpen(webSocket: WebSocket) {
        synchronized(stateLock) {
            if (closed.get() || socket !== webSocket) return
            sendRelayLocked(
                RelayMessageOuterClass.RelayMessage.newBuilder()
                    .setType(RelayMessageOuterClass.RelayMessageType.kRelayHello)
                    .setFromDeviceId(clientId)
                    .setHello(RelayMessageOuterClass.RelayHello.getDefaultInstance())
                    .build(),
            )
            sendRelayLocked(
                RelayMessageOuterClass.RelayMessage.newBuilder()
                    .setType(RelayMessageOuterClass.RelayMessageType.kRelayCreateRoom)
                    .setFromDeviceId(clientId)
                    .setCreateRoom(
                        RelayMessageOuterClass.RelayCreateRoomMessage.newBuilder()
                            .setDeviceId(clientId)
                            .setRemoteDeviceId(parameters.remoteDeviceId)
                            .setDeviceName(CLIENT_NAME)
                            .setStreamId(parameters.streamId),
                    )
                    .build(),
            )
            heartbeatJob = scope.launch {
                while (isActive && !closed.get()) {
                    delay(HEARTBEAT_INTERVAL_MILLIS)
                    synchronized(stateLock) {
                        if (!closed.get()) {
                            sendRelayLocked(
                                RelayMessageOuterClass.RelayMessage.newBuilder()
                                    .setType(RelayMessageOuterClass.RelayMessageType.kRelayHeartBeat)
                                    .setFromDeviceId(clientId)
                                    .setHeartbeat(
                                        RelayMessageOuterClass.RelayHeartBeat.newBuilder()
                                            .setIndex(heartbeatIndex++),
                                    )
                                    .build(),
                            )
                        }
                    }
                }
            }
        }
    }

    private fun onSocketMessage(bytes: OkioByteString) {
        val message = runCatching { RelayMessageOuterClass.RelayMessage.parseFrom(bytes.toByteArray()) }.getOrNull()
            ?: return fail("RTC signaling received an invalid Relay message")
        synchronized(stateLock) {
            if (closed.get()) return
            when (message.type) {
                RelayMessageOuterClass.RelayMessageType.kRelayCreateRoomResp -> {
                    val nextRoomId = message.createRoomResp.roomId
                    if (nextRoomId.isBlank()) return fail("RTC signaling received an empty room")
                    roomId = nextRoomId
                    sendRelayLocked(
                        RelayMessageOuterClass.RelayMessage.newBuilder()
                            .setType(RelayMessageOuterClass.RelayMessageType.kRelayRequestControl)
                            .setFromDeviceId(clientId)
                            .setRequestControl(
                                RelayMessageOuterClass.RelayRequestControlMessage.newBuilder()
                                    .setDeviceId(clientId)
                                    .setRemoteDeviceId(parameters.remoteDeviceId)
                                    .setRoomId(roomId)
                                    .setDeviceName(CLIENT_NAME)
                                    .setStreamId(parameters.streamId)
                                    .setForceGdi(false),
                            )
                            .build(),
                    )
                }

                RelayMessageOuterClass.RelayMessageType.kRelayRoomPrepared -> roomReady.complete(Unit)
                RelayMessageOuterClass.RelayMessageType.kRelayTargetMessage -> onPxMessageLocked(message.relay.payload)
                RelayMessageOuterClass.RelayMessageType.kRelayError -> {
                    fail(message.relayError.message.ifBlank { "RTC signaling Relay rejected the session" })
                }

                else -> Unit
            }
        }
    }

    private fun onPxMessageLocked(bytes: ByteString) {
        val message = runCatching { PxMessage.Message.parseFrom(bytes) }.getOrNull()
            ?: return fail("RTC signaling received an invalid protocol message")
        when (message.type) {
            PxMessage.MessageType.kSigAnswerSdpMessage -> {
                val answer = message.sigAnswerSdp
                val pending = pendingAnswer ?: return
                pendingAnswer = null
                if (answer.errorCode.isNotBlank()) {
                    pending.completeExceptionally(IllegalStateException(answer.errorCode))
                } else if (answer.sdp.isNotBlank()) {
                    pending.complete(answer.sdp)
                }
            }

            PxMessage.MessageType.kSigIceMessage -> {
                val ice = message.sigIce
                if (ice.ice.isNotBlank()) {
                    onEvent(StandardRtcSignalEvent.RemoteIce(IceCandidate(ice.mid, ice.sdpMlineIndex, ice.ice)))
                }
            }

            else -> Unit
        }
    }

    private fun buildOffer(sdp: String): PxMessage.Message = PxMessage.Message.newBuilder()
        .setDeviceId(clientId)
        .setStreamId(parameters.streamId)
        .setType(PxMessage.MessageType.kSigOfferSdpMessage)
        .setSigOfferSdp(
            PxSignalingMessage.SigOfferSdpMessage.newBuilder()
                .setDeviceId(clientId)
                .setSdp(sdp)
                .setConnectionTicket(parameters.ticket)
                .setClientNonce(parameters.clientNonce)
                .setInstanceId(parameters.instanceId)
                .setTakeover(false),
        )
        .build()

    private fun sendIceLocked(candidate: IceCandidate) {
        sendPxMessageLocked(
            PxMessage.Message.newBuilder()
                .setDeviceId(clientId)
                .setStreamId(parameters.streamId)
                .setType(PxMessage.MessageType.kSigIceMessage)
                .setSigIce(
                    PxSignalingMessage.SigIceMessage.newBuilder()
                        .setDeviceId(clientId)
                        .setIce(candidate.sdp)
                        .setMid(candidate.sdpMid.orEmpty())
                        .setSdpMlineIndex(candidate.sdpMLineIndex),
                )
                .build(),
        )
    }

    private fun sendPxMessageLocked(message: PxMessage.Message) {
        if (roomId.isBlank()) return
        sendRelayLocked(
            RelayMessageOuterClass.RelayMessage.newBuilder()
                .setType(RelayMessageOuterClass.RelayMessageType.kRelayTargetMessage)
                .setFromDeviceId(clientId)
                .setRelay(
                    RelayMessageOuterClass.RelayTargetMessage.newBuilder()
                        .setRelayMsgIndex(relayIndex++)
                        .addRoomIds(roomId)
                        .setPayload(ByteString.copyFrom(message.toByteArray())),
                )
                .build(),
        )
    }

    private fun sendRelayLocked(message: RelayMessageOuterClass.RelayMessage) {
        socket?.send(OkioByteString.of(*message.toByteArray()))
    }

    private fun fail(reason: String, report: Boolean = true) {
        if (!closed.compareAndSet(false, true)) return
        val activeSocket: WebSocket?
        val answer: CompletableDeferred<String>?
        synchronized(stateLock) {
            heartbeatJob?.cancel()
            heartbeatJob = null
            activeSocket = socket
            socket = null
            roomId = ""
            pendingLocalIce.clear()
            canSendIce = false
            answer = pendingAnswer
            pendingAnswer = null
        }
        val failure = IllegalStateException(reason)
        roomReady.completeExceptionally(failure)
        answer?.completeExceptionally(failure)
        activeSocket?.close(NORMAL_CLOSE_CODE, reason.take(MAX_CLOSE_REASON_CHARS))
        if (report) onEvent(StandardRtcSignalEvent.Closed(reason))
    }

    private inner class Listener : WebSocketListener() {
        override fun onOpen(webSocket: WebSocket, response: Response) = onSocketOpen(webSocket)

        override fun onMessage(webSocket: WebSocket, bytes: OkioByteString) = onSocketMessage(bytes)

        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
            webSocket.close(code, reason)
        }

        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
            if (!closed.get()) fail(reason.ifBlank { "RTC signaling closed" })
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
            fail(t.message.orEmpty().ifBlank { "RTC signaling connection failed" })
        }
    }
}

internal fun buildRequestUrl(parameters: StandardRtcSignalParameters, clientId: String): HttpUrl = HttpUrl.Builder()
    // OkHttp represents WebSocket request URLs as HTTP(S); newWebSocket
    // performs the corresponding ws/wss upgrade.
    .scheme(if (parameters.secure) "https" else "http")
    .host(parameters.relayHost)
    .port(parameters.relayPort)
    .addPathSegment("relay")
    .addQueryParameter("device_id", clientId)
    .addQueryParameter("remote_device_id", parameters.remoteDeviceId)
    .addQueryParameter("ticket_device_id", parameters.ticketDeviceId)
    .addQueryParameter("device_name", CLIENT_NAME)
    .addQueryParameter("stream_id", parameters.streamId)
    .addQueryParameter("rtc_signal", "1")
    .addQueryParameter("ticket", parameters.ticket)
    .addQueryParameter("client_nonce", parameters.clientNonce)
    .apply {
        if (parameters.instanceId.isNotBlank()) addQueryParameter("instance_id", parameters.instanceId)
    }
    .build()

private const val CLIENT_NAME = "PixelsAndroid"
private const val CONNECT_TIMEOUT_MILLIS = 10_000L
private const val ANSWER_TIMEOUT_MILLIS = 15_000L
private const val HEARTBEAT_INTERVAL_MILLIS = 1_000L
private const val MAX_PENDING_ICE = 256
private const val NORMAL_CLOSE_CODE = 1000
private const val MAX_CLOSE_REASON_CHARS = 120
