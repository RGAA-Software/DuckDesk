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
import yun.pixels.client.core.domain.session.PointerAction
import yun.pixels.client.core.domain.session.RemoteSessionCapabilities
import yun.pixels.client.core.domain.session.RemoteSessionFailure
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionTarget
import yun.pixels.client.core.domain.session.RemoteSessionTransport
import yun.pixels.client.core.domain.session.RemoteTransportEvent
import yun.pixels.client.core.domain.session.RemoteTransportStartResult
import yun.pixels.client.core.domain.session.RemoteVideoSize
import java.net.URI
import java.net.URLDecoder

class NativeRemoteSessionTransport(
    private val installationIdentity: InstallationIdentity,
    private val callbackScope: CoroutineScope,
) : RemoteSessionTransport, NativeSessionListener {
    private val lock = Mutex()
    private val mutableEvents = MutableSharedFlow<RemoteTransportEvent>(extraBufferCapacity = 32)
    private val surfaces = mutableMapOf<RemoteSessionId, Surface>()
    private val nativeSessionIds = mutableMapOf<RemoteSessionId, Long>()

    override val events: Flow<RemoteTransportEvent> = mutableEvents.asSharedFlow()

    suspend fun attachSurface(sessionId: RemoteSessionId, surface: Surface) {
        val nativeSessionId = lock.withLock {
            surfaces[sessionId] = surface
            nativeSessionIds[sessionId]
        }
        if (nativeSessionId != null) withContext(Dispatchers.IO) { PixelsNativeBridge.replaceSurface(nativeSessionId, surface) }
    }

    suspend fun detachSurface(sessionId: RemoteSessionId) {
        lock.withLock { surfaces.remove(sessionId) }
    }

    override suspend fun start(request: RemoteSessionRequest): RemoteTransportStartResult {
        val surface = lock.withLock {
            if (nativeSessionIds.containsKey(request.id)) return RemoteTransportStartResult.Accepted
            surfaces[request.id]
        } ?: return RemoteTransportStartResult.Rejected(RemoteSessionFailure.DecoderUnavailable)
        val config = request.toNativeConfig(installationIdentity.value())
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
            nativeSessionIds.remove(sessionId)
        } ?: return
        withContext(Dispatchers.IO) { PixelsNativeBridge.stop(nativeSessionId) }
    }

    override suspend fun sendInput(sessionId: RemoteSessionId, command: InputCommand): Boolean {
        val nativeSessionId = lock.withLock { nativeSessionIds[sessionId] } ?: return false
        return when (command) {
            is InputCommand.Pointer -> withContext(Dispatchers.IO) {
                PixelsNativeBridge.sendPointer(nativeSessionId, command.action.nativeValue, command.xRatio, command.yRatio)
            }
        }
    }

    override fun onConnected(
        sessionId: String,
        activeMonitorName: String,
        supportsAudio: Boolean,
        supportsInput: Boolean,
        supportsFileTransfer: Boolean,
        supportsClipboard: Boolean,
    ) {
        callbackScope.launch {
            mutableEvents.emit(
                RemoteTransportEvent.Connected(
                    sessionId = RemoteSessionId(sessionId),
                    capabilities = RemoteSessionCapabilities(
                        monitorNames = listOf(activeMonitorName).filter(String::isNotBlank),
                        activeMonitorName = activeMonitorName,
                        supportsAudio = supportsAudio,
                        supportsInput = supportsInput,
                        supportsFileTransfer = supportsFileTransfer,
                        supportsClipboard = supportsClipboard,
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

private fun RemoteSessionRequest.toNativeConfig(clientDeviceId: String): NativeSessionConfig? {
    val endpoint = when (val sessionTarget = target) {
        is RemoteSessionTarget.Direct -> NativeEndpoint(
            host = sessionTarget.device.endpoint.host,
            port = sessionTarget.device.endpoint.renderPort,
            ssl = false,
            remoteDeviceId = sessionTarget.device.id.value,
            streamId = id.value,
            randomPassword = sessionTarget.credential.orEmpty(),
        )
        is RemoteSessionTarget.Account -> sessionTarget.connectionTicket.toNativeEndpoint(sessionTarget.device.deviceId) ?: return null
    }
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
        connectionNonce = accountTarget?.clientNonce.orEmpty(),
        rtcIceConfigJson = accountTarget?.connectionTicket?.rtcIceConfigJson.orEmpty(),
        relayHost = accountTarget?.connectionTicket?.relayHost.orEmpty(),
        relayPort = accountTarget?.connectionTicket?.relayPort ?: 0,
        enableVideo = enableVideo,
        enableAudio = enableAudio,
        enableInput = enableInput,
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

private fun String.isPrivateOrCarrierGradeAddress(): Boolean {
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

private val PointerAction.nativeValue: Int
    get() = when (this) {
        PointerAction.Down -> 0
        PointerAction.Up -> 1
        PointerAction.Move -> 2
    }
