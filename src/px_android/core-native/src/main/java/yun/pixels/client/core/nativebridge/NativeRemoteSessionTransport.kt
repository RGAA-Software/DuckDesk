package yun.pixels.client.core.nativebridge

import android.view.Surface
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import yun.pixels.client.core.domain.session.InstallationIdentity
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.RemoteMouseButton
import yun.pixels.client.core.domain.session.RemoteSessionCapabilities
import yun.pixels.client.core.domain.session.RemoteSessionFailure
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionStatistics
import yun.pixels.client.core.domain.session.RemoteSessionTarget
import yun.pixels.client.core.domain.session.RemoteSessionTransport
import yun.pixels.client.core.domain.session.RemoteTransportEvent
import yun.pixels.client.core.domain.session.RemoteTransportStartResult
import yun.pixels.client.core.domain.session.RemoteVideoSize
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayOperation
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayResult
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayResultState
import yun.pixels.client.core.domain.transfer.FileTransferDirection
import yun.pixels.client.core.domain.transfer.FileTransferEvent
import yun.pixels.client.core.domain.transfer.FileTransferTransport
import yun.pixels.client.core.domain.recording.RecordingEvent
import yun.pixels.client.core.domain.recording.RecordingId
import yun.pixels.client.core.domain.recording.RecordingTransport
import java.net.URI
import java.net.URLDecoder

class NativeRemoteSessionTransport internal constructor(
    private val installationIdentity: InstallationIdentity,
    private val callbackScope: CoroutineScope,
    private val directSessionAuthorizer: DirectSessionAuthorizer,
) : RemoteSessionTransport, FileTransferTransport, RecordingTransport, NativeSessionListener {
    constructor(installationIdentity: InstallationIdentity, callbackScope: CoroutineScope) : this(
        installationIdentity,
        callbackScope,
        HttpDirectSessionAuthorizer(),
    )

    private val lock = Mutex()
    private val surfaceLock = Mutex()
    private val mutableEvents = MutableSharedFlow<RemoteTransportEvent>(extraBufferCapacity = 32)
    private val mutableFileTransferEvents = MutableSharedFlow<FileTransferEvent>(extraBufferCapacity = 64)
    private val mutableRecordingEvents = MutableSharedFlow<RecordingEvent>(extraBufferCapacity = 8)
    private val surfaces = mutableMapOf<RemoteSessionId, Surface>()
    private val nativeSessionIds = mutableMapOf<RemoteSessionId, Long>()
    private val capabilities = mutableMapOf<RemoteSessionId, RemoteSessionCapabilities>()

    override val events: Flow<RemoteTransportEvent> = mutableEvents.asSharedFlow()
    override val fileTransferEvents: Flow<FileTransferEvent> = mutableFileTransferEvents.asSharedFlow()
    override val recordingEvents: Flow<RecordingEvent> = mutableRecordingEvents.asSharedFlow()

    suspend fun attachSurface(sessionId: RemoteSessionId, surface: Surface) {
        surfaceLock.withLock {
            val nativeSessionId = lock.withLock {
                surfaces[sessionId] = surface
                nativeSessionIds[sessionId]
            }
            if (nativeSessionId != null) withContext(Dispatchers.IO) { PixelsNativeBridge.replaceSurface(nativeSessionId, surface) }
        }
    }

    suspend fun detachSurface(sessionId: RemoteSessionId, surface: Surface) {
        surfaceLock.withLock {
            val nativeSessionId = lock.withLock {
                if (surfaces[sessionId] !== surface) return
                surfaces.remove(sessionId)
                nativeSessionIds[sessionId]
            }
            if (nativeSessionId != null) withContext(Dispatchers.IO) { PixelsNativeBridge.detachSurface(nativeSessionId) }
        }
    }

    override suspend fun start(request: RemoteSessionRequest): RemoteTransportStartResult {
        val surface = lock.withLock {
            if (nativeSessionIds.containsKey(request.id)) return RemoteTransportStartResult.Accepted
            surfaces[request.id]
        } ?: return RemoteTransportStartResult.Rejected(RemoteSessionFailure.DecoderUnavailable)
        val directAuthorization = when (val target = request.target) {
            is RemoteSessionTarget.Direct -> when (val result = directSessionAuthorizer.authorize(target.device.endpoint, target.credential.orEmpty())) {
                is DirectSessionAuthorizationResult.Authorized -> result.value
                DirectSessionAuthorizationResult.Rejected -> return RemoteTransportStartResult.Rejected(RemoteSessionFailure.AuthenticationRejected)
                DirectSessionAuthorizationResult.Unavailable -> return RemoteTransportStartResult.Rejected(RemoteSessionFailure.NetworkUnavailable)
            }
            is RemoteSessionTarget.Account -> null
        }
        val config = request.toNativeConfig(installationIdentity.value(), directAuthorization)
            ?: return RemoteTransportStartResult.Rejected(RemoteSessionFailure.InvalidRequest)
        val nativeSessionId = withContext(Dispatchers.IO) { PixelsNativeBridge.create(config, this@NativeRemoteSessionTransport, surface) }
        if (nativeSessionId == 0L) return RemoteTransportStartResult.Rejected(RemoteSessionFailure.TransportUnavailable)
        val accepted = lock.withLock {
            if (nativeSessionIds.containsKey(request.id)) false else {
                nativeSessionIds[request.id] = nativeSessionId
                true
            }
        }
        if (!accepted) {
            withContext(Dispatchers.IO) { PixelsNativeBridge.stop(nativeSessionId) }
            return RemoteTransportStartResult.Accepted
        }
        if (withContext(Dispatchers.IO) { PixelsNativeBridge.start(nativeSessionId) }) return RemoteTransportStartResult.Accepted
        lock.withLock { nativeSessionIds.remove(request.id, nativeSessionId) }
        withContext(Dispatchers.IO) { PixelsNativeBridge.stop(nativeSessionId) }
        return RemoteTransportStartResult.Rejected(RemoteSessionFailure.TransportUnavailable)
    }

    override suspend fun stop(sessionId: RemoteSessionId) {
        val nativeSessionId = lock.withLock {
            surfaces.remove(sessionId)
            capabilities.remove(sessionId)
            nativeSessionIds.remove(sessionId)
        } ?: return
        withContext(Dispatchers.IO) { PixelsNativeBridge.stop(nativeSessionId) }
    }

    override suspend fun sendInput(sessionId: RemoteSessionId, command: InputCommand): Boolean {
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return when (command) {
            is InputCommand.MoveAbsolute -> sendMouse(nativeSessionId, MOUSE_MOVE_ABSOLUTE, xRatio = command.xRatio, yRatio = command.yRatio)
            is InputCommand.MoveRelative -> sendMouse(
                nativeSessionId,
                MOUSE_MOVE_RELATIVE,
                xRatio = command.deltaXRatio,
                yRatio = command.deltaYRatio,
            )
            is InputCommand.MouseButton -> sendMouse(
                nativeSessionId,
                MOUSE_BUTTON,
                button = command.button.nativeValue,
                down = command.down,
                xRatio = command.xRatio ?: Float.NaN,
                yRatio = command.yRatio ?: Float.NaN,
            )
            is InputCommand.Wheel -> sendMouse(
                nativeSessionId,
                MOUSE_WHEEL,
                deltaX = command.deltaX,
                deltaY = command.deltaY,
            )
            is InputCommand.Key -> PixelsNativeBridge.sendKey(nativeSessionId, command.key.virtualKeyCode, command.down)
            is InputCommand.Gamepad -> command.state.let { state ->
                PixelsNativeBridge.sendGamepad(
                    nativeSessionId,
                    state.buttons,
                    state.leftTrigger,
                    state.rightTrigger,
                    state.leftThumbX,
                    state.leftThumbY,
                    state.rightThumbX,
                    state.rightThumbY,
                )
            }
            is InputCommand.Text -> PixelsNativeBridge.sendText(nativeSessionId, command.value.encodeToByteArray())
            InputCommand.SecureAttention -> PixelsNativeBridge.sendSecureAttention(nativeSessionId)
        }
    }

    override suspend fun sendClipboardText(sessionId: RemoteSessionId, value: String): Boolean {
        val encoded = value.encodeToByteArray()
        if (encoded.isEmpty() || encoded.size > MAX_CLIPBOARD_TEXT_BYTES) return false
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return PixelsNativeBridge.sendClipboardText(nativeSessionId, encoded)
    }

    override suspend fun startUpload(sessionId: RemoteSessionId, localPath: String, remoteDirectory: String): Int? {
        if (localPath.isBlank() || remoteDirectory.isBlank()) return null
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return null
        return withContext(Dispatchers.IO) {
            PixelsNativeBridge.startFileUpload(nativeSessionId, localPath.encodeToByteArray(), remoteDirectory.encodeToByteArray())
                .takeIf { it > 0 }
        }
    }

    override suspend fun startDownload(sessionId: RemoteSessionId, remotePath: String, localDirectory: String): Int? {
        if (remotePath.isBlank() || localDirectory.isBlank()) return null
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return null
        return withContext(Dispatchers.IO) {
            PixelsNativeBridge.startFileDownload(nativeSessionId, remotePath.encodeToByteArray(), localDirectory.encodeToByteArray())
                .takeIf { it > 0 }
        }
    }

    override suspend fun cancelTransfer(sessionId: RemoteSessionId, jobId: Int): Boolean {
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return withContext(Dispatchers.IO) { PixelsNativeBridge.cancelFileTransfer(nativeSessionId, jobId) }
    }

    override suspend fun confirmOverwrite(
        sessionId: RemoteSessionId,
        jobId: Int,
        fileNumber: Int,
        overwrite: Boolean,
        offsetBytes: Long,
        applyToAll: Boolean,
    ): Boolean {
        if (offsetBytes < 0) return false
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return withContext(Dispatchers.IO) {
            PixelsNativeBridge.confirmFileOverwrite(nativeSessionId, jobId, fileNumber, overwrite, offsetBytes, applyToAll)
        }
    }

    override suspend fun startRecording(
        sessionId: RemoteSessionId,
        recordingId: RecordingId,
        stagingDirectory: String,
    ): Boolean {
        if (stagingDirectory.isBlank()) return false
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return withContext(Dispatchers.IO) {
            PixelsNativeBridge.startRecording(
                nativeSessionId,
                recordingId.value.encodeToByteArray(),
                stagingDirectory.encodeToByteArray(),
            )
        }
    }

    override suspend fun stopRecording(sessionId: RemoteSessionId, recordingId: RecordingId): Boolean {
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return withContext(Dispatchers.IO) {
            PixelsNativeBridge.stopRecording(nativeSessionId, recordingId.value.encodeToByteArray())
        }
    }

    private suspend fun sendMouse(
        nativeSessionId: Long,
        action: Int,
        button: Int = 0,
        down: Boolean = false,
        xRatio: Float = 0f,
        yRatio: Float = 0f,
        deltaX: Int = 0,
        deltaY: Int = 0,
    ): Boolean = PixelsNativeBridge.sendMouse(nativeSessionId, action, button, down, xRatio, yRatio, deltaX, deltaY)

    suspend fun switchMonitor(sessionId: RemoteSessionId, monitorName: String): Boolean {
        if (monitorName.isBlank()) return false
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return PixelsNativeBridge.switchMonitor(nativeSessionId, monitorName)
    }

    suspend fun requestVirtualDisplay(
        sessionId: RemoteSessionId,
        requestId: String,
        operation: RemoteVirtualDisplayOperation,
    ): Boolean {
        if (requestId.isBlank()) return false
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return PixelsNativeBridge.requestVirtualDisplay(
            nativeSessionId,
            requestId,
            operation.nativeValue,
            DEFAULT_VIRTUAL_DISPLAY_WIDTH,
            DEFAULT_VIRTUAL_DISPLAY_HEIGHT,
            DEFAULT_VIRTUAL_DISPLAY_REFRESH_HZ,
        )
    }

    suspend fun setAudioEnabled(sessionId: RemoteSessionId, enabled: Boolean): Boolean {
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return withContext(Dispatchers.IO) { PixelsNativeBridge.setAudioEnabled(nativeSessionId, enabled) }
    }

    override fun onConnected(
        sessionId: String,
        monitorNames: Array<String>,
        activeMonitorName: String,
        supportsAudio: Boolean,
        supportsInput: Boolean,
        supportsFileTransfer: Boolean,
        supportsClipboard: Boolean,
        supportsVirtualDisplays: Boolean,
        ownedVirtualDisplayCount: Int,
        maximumVirtualDisplayCount: Int,
        topologyGeneration: Long,
    ) {
        callbackScope.launch {
            val remoteSessionId = RemoteSessionId(sessionId)
            val sessionCapabilities = RemoteSessionCapabilities(
                monitorNames = monitorNames.filter(String::isNotBlank).distinct(),
                activeMonitorName = activeMonitorName,
                supportsAudio = supportsAudio,
                supportsInput = supportsInput,
                supportsFileTransfer = supportsFileTransfer,
                supportsClipboard = supportsClipboard,
                supportsVirtualDisplays = supportsVirtualDisplays,
                ownedVirtualDisplayCount = ownedVirtualDisplayCount.coerceAtLeast(0),
                maximumVirtualDisplayCount = maximumVirtualDisplayCount.coerceAtLeast(0),
                topologyGeneration = topologyGeneration.coerceAtLeast(0),
            )
            lock.withLock { capabilities[remoteSessionId] = sessionCapabilities }
            mutableEvents.emit(
                RemoteTransportEvent.Connected(
                    sessionId = remoteSessionId,
                    capabilities = sessionCapabilities,
                ),
            )
        }
    }

    override fun onMonitorsChanged(sessionId: String, monitorNames: Array<String>, activeMonitorName: String) {
        callbackScope.launch {
            val remoteSessionId = RemoteSessionId(sessionId)
            val updated = lock.withLock {
                val current = capabilities[remoteSessionId] ?: return@withLock null
                current.copy(
                    monitorNames = monitorNames.filter(String::isNotBlank).distinct(),
                    activeMonitorName = activeMonitorName,
                ).also { capabilities[remoteSessionId] = it }
            } ?: return@launch
            mutableEvents.emit(RemoteTransportEvent.CapabilitiesUpdated(remoteSessionId, updated))
        }
    }

    override fun onVirtualDisplayResult(
        sessionId: String,
        requestId: String,
        accepted: Boolean,
        state: Int,
        topologyChanged: Boolean,
        topologyGeneration: Long,
        ownedDisplayCount: Int,
        errorCode: String,
        errorMessage: String,
    ) {
        callbackScope.launch {
            val remoteSessionId = RemoteSessionId(sessionId)
            val updated = lock.withLock {
                capabilities[remoteSessionId]?.copy(
                    ownedVirtualDisplayCount = ownedDisplayCount.coerceAtLeast(0),
                    topologyGeneration = topologyGeneration.coerceAtLeast(0),
                )?.also { capabilities[remoteSessionId] = it }
            }
            if (updated != null) mutableEvents.emit(RemoteTransportEvent.CapabilitiesUpdated(remoteSessionId, updated))
            mutableEvents.emit(
                RemoteTransportEvent.VirtualDisplayResult(
                    remoteSessionId,
                    RemoteVirtualDisplayResult(
                        requestId = requestId,
                        accepted = accepted,
                        state = state.toVirtualDisplayResultState(),
                        topologyChanged = topologyChanged,
                        topologyGeneration = topologyGeneration.coerceAtLeast(0),
                        ownedDisplayCount = ownedDisplayCount.coerceAtLeast(0),
                        errorCode = errorCode,
                        errorMessage = errorMessage,
                    ),
                ),
            )
        }
    }

    override fun onFrameSizeChanged(sessionId: String, width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        callbackScope.launch {
            mutableEvents.emit(RemoteTransportEvent.VideoSize(RemoteSessionId(sessionId), RemoteVideoSize(width, height)))
        }
    }

    override fun onStatistics(sessionId: String, framesPerSecond: Int, latencyMillis: Int, bitrateKbps: Int) {
        callbackScope.launch {
            mutableEvents.emit(
                RemoteTransportEvent.Statistics(
                    sessionId = RemoteSessionId(sessionId),
                    value = RemoteSessionStatistics(
                        framesPerSecond = framesPerSecond.coerceAtLeast(0),
                        latencyMillis = latencyMillis.coerceAtLeast(0),
                        bitrateKbps = bitrateKbps.coerceAtLeast(0),
                    ),
                ),
            )
        }
    }

    override fun onClipboardText(sessionId: String, utf8Text: ByteArray) {
        if (utf8Text.isEmpty() || utf8Text.size > MAX_CLIPBOARD_TEXT_BYTES) return
        val value = runCatching { utf8Text.decodeToString(throwOnInvalidSequence = true) }.getOrNull()?.takeIf(String::isNotEmpty) ?: return
        callbackScope.launch { mutableEvents.emit(RemoteTransportEvent.ClipboardText(RemoteSessionId(sessionId), value)) }
    }

    override fun onFileTransferProgress(
        sessionId: String,
        jobId: Int,
        fileNumber: Int,
        fileCount: Int,
        totalSize: Long,
        finishedSize: Long,
        transferred: Long,
        speedBytesPerSecond: Double,
        download: Boolean,
    ) {
        if (jobId <= 0) return
        callbackScope.launch {
            mutableFileTransferEvents.emit(
                FileTransferEvent.Progress(
                    sessionId = RemoteSessionId(sessionId),
                    jobId = jobId,
                    fileNumber = fileNumber.coerceAtLeast(0),
                    fileCount = fileCount.coerceAtLeast(0),
                    totalBytes = totalSize.coerceAtLeast(0),
                    completedBytes = finishedSize.coerceAtLeast(0),
                    transferredBytes = transferred.coerceAtLeast(0),
                    speedBytesPerSecond = speedBytesPerSecond.coerceAtLeast(0.0),
                    direction = if (download) FileTransferDirection.Download else FileTransferDirection.Upload,
                ),
            )
        }
    }

    override fun onFileTransferDone(sessionId: String, jobId: Int, utf8Error: ByteArray) {
        if (jobId <= 0) return
        val error = utf8Error.decodeUtf8OrEmpty()
        callbackScope.launch { mutableFileTransferEvents.emit(FileTransferEvent.Completed(RemoteSessionId(sessionId), jobId, error)) }
    }

    override fun onFileTransferOverwrite(
        sessionId: String,
        jobId: Int,
        fileNumber: Int,
        utf8Path: ByteArray,
        upload: Boolean,
        identical: Boolean,
    ) {
        if (jobId <= 0) return
        callbackScope.launch {
            mutableFileTransferEvents.emit(
                FileTransferEvent.OverwriteRequired(
                    sessionId = RemoteSessionId(sessionId),
                    jobId = jobId,
                    fileNumber = fileNumber.coerceAtLeast(0),
                    path = utf8Path.decodeUtf8OrEmpty(),
                    upload = upload,
                    identical = identical,
                ),
            )
        }
    }

    override fun onRecordingState(sessionId: String, recordingId: String, state: Int, utf8Error: ByteArray) {
        val id = runCatching { RecordingId(recordingId) }.getOrNull() ?: return
        val remoteSessionId = RemoteSessionId(sessionId)
        val event = when (state) {
            RECORDING_STARTED -> RecordingEvent.Started(remoteSessionId, id)
            RECORDING_COMPLETED, RECORDING_FAILED -> RecordingEvent.Finished(remoteSessionId, id, utf8Error.decodeUtf8OrEmpty())
            else -> return
        }
        callbackScope.launch { mutableRecordingEvents.emit(event) }
    }

    override fun onDisconnected(sessionId: String, reason: Int, recoverable: Boolean) {
        callbackScope.launch {
            mutableEvents.emit(
                RemoteTransportEvent.Disconnected(
                    sessionId = RemoteSessionId(sessionId),
                    reason = reason.toFailure(),
                    recoverable = recoverable,
                ),
            )
        }
    }
}

private fun RemoteSessionRequest.toNativeConfig(
    clientDeviceId: String,
    directAuthorization: DirectSessionAuthorization?,
): NativeSessionConfig? {
    val endpoint = when (val sessionTarget = target) {
        is RemoteSessionTarget.Direct -> {
            val directEndpoint = sessionTarget.device.endpoint
            if (!directEndpoint.host.isPrivateOrCarrierGradeAddress() || directEndpoint.renderPort !in 1..65535) return null
            NativeEndpoint(
                host = directEndpoint.host,
                port = directEndpoint.renderPort,
                ssl = false,
                remoteDeviceId = sessionTarget.device.id.value,
                streamId = directAuthorization?.streamId ?: return null,
                randomPassword = "",
            )
        }
        is RemoteSessionTarget.Account -> sessionTarget.connectionTicket.toNativeEndpoint(sessionTarget.fallbackRemoteDeviceId) ?: return null
    }
    if (endpoint.remoteDeviceId.isBlank() || endpoint.streamId.isBlank() || clientDeviceId.isBlank()) return null
    val accountTarget = target as? RemoteSessionTarget.Account
    return NativeSessionConfig(
        sessionId = id.value,
        host = endpoint.host,
        port = endpoint.port,
        ssl = endpoint.ssl,
        remoteDeviceId = endpoint.remoteDeviceId,
        displayName = target.displayName,
        streamId = endpoint.streamId,
        clientDeviceId = clientDeviceId,
        randomPassword = endpoint.randomPassword,
        connectionTicket = accountTarget?.connectionTicket?.ticket.orEmpty(),
        connectionNonce = accountTarget?.clientNonce ?: directAuthorization?.clientNonce.orEmpty(),
        rtcIceConfigJson = accountTarget?.connectionTicket?.rtcIceConfigJson.orEmpty(),
        relayHost = accountTarget?.connectionTicket?.relayHost.orEmpty(),
        relayPort = accountTarget?.connectionTicket?.relayPort ?: 0,
        enableVideo = enableVideo,
        enableAudio = enableAudio,
        enableInput = enableInput,
        enableClipboard = enableClipboard && when (val sessionTarget = target) {
            is RemoteSessionTarget.Direct -> true
            is RemoteSessionTarget.Account -> "clipboard" in sessionTarget.connectionTicket.permissions
        },
    )
}

internal fun yun.pixels.client.core.domain.account.ConnectionTicket.toNativeEndpoint(fallbackDeviceId: String): NativeEndpoint? {
    val uri = runCatching { URI(launchUrl) }.getOrNull() ?: return null
    if (uri.scheme?.lowercase() !in setOf("http", "https")) return null
    val host = uri.host?.takeIf(String::isNotBlank) ?: return null
    val ssl = uri.scheme.equals("https", ignoreCase = true)
    if (!ssl && !host.isPrivateOrCarrierGradeAddress()) return null
    val port = uri.port.takeIf { it in 1..65535 } ?: if (ssl) 443 else 80
    return NativeEndpoint(
        host = host,
        port = port,
        ssl = ssl,
        remoteDeviceId = uri.queryParameter("deviceId").orEmpty().ifBlank { fallbackDeviceId },
        streamId = streamId,
        randomPassword = "",
    )
}

internal data class NativeEndpoint(
    val host: String,
    val port: Int,
    val ssl: Boolean,
    val remoteDeviceId: String,
    val streamId: String,
    val randomPassword: String,
)

private fun URI.queryParameter(name: String): String? = rawQuery
    ?.split('&')
    ?.asSequence()
    ?.map { component -> component.substringBefore('=') to component.substringAfter('=', "") }
    ?.firstOrNull { (key) -> URLDecoder.decode(key, "UTF-8") == name }
    ?.second
    ?.let { URLDecoder.decode(it, "UTF-8") }

internal fun String.isPrivateOrCarrierGradeAddress(): Boolean {
    val octets = split('.').mapNotNull(String::toIntOrNull)
    if (octets.size == 4 && octets.all { it in 0..255 }) {
        return octets[0] == 10 ||
            octets[0] == 172 && octets[1] in 16..31 ||
            octets[0] == 192 && octets[1] == 168 ||
            octets[0] == 100 && octets[1] in 64..127 ||
            octets[0] == 169 && octets[1] == 254
    }
    if (!contains(':')) return false
    val bytes = runCatching { java.net.InetAddress.getByName(this).address }.getOrNull() ?: return false
    return bytes.size == 16 && ((bytes[0].toInt() and 0xfe) == 0xfc || bytes[0] == 0xfe.toByte() && (bytes[1].toInt() and 0xc0) == 0x80)
}

private fun Int.toFailure(): RemoteSessionFailure = when (this) {
    1 -> RemoteSessionFailure.AuthenticationRejected
    2 -> RemoteSessionFailure.DeviceOffline
    3 -> RemoteSessionFailure.NetworkUnavailable
    4 -> RemoteSessionFailure.DecoderUnavailable
    5 -> RemoteSessionFailure.ProtocolError
    else -> RemoteSessionFailure.RemoteEnded
}

private val RemoteMouseButton.nativeValue: Int
    get() = when (this) {
        RemoteMouseButton.Left -> 0
        RemoteMouseButton.Middle -> 1
        RemoteMouseButton.Right -> 2
    }

private const val MOUSE_MOVE_ABSOLUTE = 0
private const val MOUSE_MOVE_RELATIVE = 1
private const val MOUSE_BUTTON = 2
private const val MOUSE_WHEEL = 3
private const val DEFAULT_VIRTUAL_DISPLAY_WIDTH = 1920
private const val DEFAULT_VIRTUAL_DISPLAY_HEIGHT = 1080
private const val DEFAULT_VIRTUAL_DISPLAY_REFRESH_HZ = 60
private const val MAX_CLIPBOARD_TEXT_BYTES = 1_048_576
private const val RECORDING_STARTED = 1
private const val RECORDING_COMPLETED = 2
private const val RECORDING_FAILED = 3

private fun ByteArray.decodeUtf8OrEmpty(): String = runCatching { decodeToString(throwOnInvalidSequence = true) }.getOrDefault("")

private val RemoteVirtualDisplayOperation.nativeValue: Int
    get() = when (this) {
        RemoteVirtualDisplayOperation.Create -> 0
        RemoteVirtualDisplayOperation.RemoveLast -> 1
    }

private fun Int.toVirtualDisplayResultState(): RemoteVirtualDisplayResultState = when (this) {
    0 -> RemoteVirtualDisplayResultState.Ready
    1 -> RemoteVirtualDisplayResultState.NeedReconnect
    else -> RemoteVirtualDisplayResultState.Failed
}
