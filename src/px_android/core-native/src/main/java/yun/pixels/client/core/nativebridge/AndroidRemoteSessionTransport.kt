package yun.pixels.client.core.nativebridge

import android.content.Context
import android.view.Surface
import java.io.Closeable
import java.net.URI
import java.net.URLDecoder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import okhttp3.OkHttpClient
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.InstallationIdentity
import yun.pixels.client.core.domain.session.LocalClipboardFile
import yun.pixels.client.core.domain.session.RemoteSessionCapabilities
import yun.pixels.client.core.domain.session.RemoteSessionFailure
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionTarget
import yun.pixels.client.core.domain.session.RemoteSessionTransport
import yun.pixels.client.core.domain.session.RemoteTransportEvent
import yun.pixels.client.core.domain.session.RemoteTransportStartResult
import yun.pixels.client.core.domain.session.RemoteVideoSize
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayOperation
import yun.pixels.client.core.domain.transfer.FileTransferTransport
import yun.pixels.client.core.domain.transfer.FileTransferEvent
import yun.pixels.client.core.domain.transfer.RemoteDirectoryEvent
import yun.pixels.client.core.domain.recording.RecordingTransport
import yun.pixels.client.core.domain.recording.RecordingEvent
import yun.pixels.client.core.domain.recording.RecordingId
import yun.pixels.client.core.domain.voice.VoiceCallTransport
import yun.pixels.client.core.domain.voice.VoiceCallEvent

/** Product transport router. Direct/LAN sessions retain the native UDP/WebSocket
 * engine; ticketed account sessions use standard WebRTC when Console supplied a
 * complete unexpired ICE configuration and otherwise use ticketed Relay. */
class AndroidRemoteSessionTransport internal constructor(
    context: Context,
    installationIdentity: InstallationIdentity,
    private val callbackScope: CoroutineScope,
    private val httpClient: OkHttpClient,
) : RemoteSessionTransport,
    FileTransferTransport,
    RecordingTransport,
    VoiceCallTransport,
    Closeable {
    private val applicationContext = context.applicationContext
    private val native = NativeRemoteSessionTransport(installationIdentity, callbackScope)
    private val lock = Mutex()
    private val mutableEvents = MutableSharedFlow<RemoteTransportEvent>(extraBufferCapacity = 64)
    private val surfaces = mutableMapOf<RemoteSessionId, Surface>()
    private val nativeSessions = mutableSetOf<RemoteSessionId>()
    private val rtcSessions = mutableMapOf<RemoteSessionId, WebRtcPeerSession>()
    private val rtcRequests = mutableMapOf<RemoteSessionId, RemoteSessionRequest>()
    private val rtcCapabilities = mutableMapOf<RemoteSessionId, RemoteSessionCapabilities>()
    private var rtcRuntime: WebRtcRuntime? = null

    constructor(
        context: Context,
        installationIdentity: InstallationIdentity,
        callbackScope: CoroutineScope,
    ) : this(context, installationIdentity, callbackScope, OkHttpClient())

    override val events: Flow<RemoteTransportEvent> = mutableEvents.asSharedFlow()
    override val fileTransferEvents: Flow<FileTransferEvent> = native.fileTransferEvents
    override val remoteDirectoryEvents: Flow<RemoteDirectoryEvent> = native.remoteDirectoryEvents
    override val recordingEvents: Flow<RecordingEvent> = native.recordingEvents
    override val voiceCallEvents: Flow<VoiceCallEvent> = native.voiceCallEvents

    init {
        callbackScope.launch { native.events.collect(mutableEvents::emit) }
    }

    suspend fun attachSurface(sessionId: RemoteSessionId, surface: Surface) {
        val (rtc, nativeActive) = lock.withLock {
            surfaces[sessionId] = surface
            rtcSessions[sessionId] to (sessionId in nativeSessions)
        }
        if (rtc != null) rtc.replaceSurface(surface) else if (nativeActive) native.attachSurface(sessionId, surface)
    }

    suspend fun detachSurface(sessionId: RemoteSessionId, surface: Surface) {
        val (rtc, nativeActive) = lock.withLock {
            if (surfaces[sessionId] !== surface) return
            surfaces.remove(sessionId)
            rtcSessions[sessionId] to (sessionId in nativeSessions)
        }
        if (rtc != null) rtc.detachSurface(surface) else if (nativeActive) native.detachSurface(sessionId, surface)
    }

    override suspend fun start(request: RemoteSessionRequest): RemoteTransportStartResult {
        val launch = request.standardRtcLaunchOrNull()
        if (launch == null) {
            val surface = lock.withLock {
                if (request.id in nativeSessions) return RemoteTransportStartResult.Accepted
                surfaces[request.id]
            } ?: return RemoteTransportStartResult.Rejected(RemoteSessionFailure.DecoderUnavailable)
            native.attachSurface(request.id, surface)
            lock.withLock { nativeSessions += request.id }
            return native.start(request).also { result ->
                if (result is RemoteTransportStartResult.Rejected) {
                    lock.withLock { nativeSessions -= request.id }
                    native.detachSurface(request.id, surface)
                }
            }
        }
        val surface = lock.withLock {
            if (rtcSessions.containsKey(request.id)) return RemoteTransportStartResult.Accepted
            surfaces[request.id]
        } ?: return RemoteTransportStartResult.Rejected(RemoteSessionFailure.DecoderUnavailable)
        val runtime = try {
            lock.withLock { rtcRuntime ?: WebRtcRuntime(applicationContext).also { rtcRuntime = it } }
        } catch (_: Throwable) {
            return RemoteTransportStartResult.Rejected(RemoteSessionFailure.DecoderUnavailable)
        }
        val session = WebRtcPeerSession(
            runtime = runtime,
            httpClient = httpClient,
            scope = callbackScope,
            parameters = launch.parameters,
            iceConfiguration = launch.iceConfiguration,
            surface = surface,
            enableVideo = request.enableVideo,
            enableAudio = request.enableAudio,
            enableInput = request.enableInput && "input" in launch.permissions,
            onEvent = { event -> callbackScope.launch { handleRtcEvent(request.id, event) } },
        )
        val accepted = lock.withLock {
            if (rtcSessions.containsKey(request.id)) false else {
                rtcSessions[request.id] = session
                rtcRequests[request.id] = request
                true
            }
        }
        if (!accepted) {
            session.close()
            return RemoteTransportStartResult.Accepted
        }
        return try {
            session.start()
            RemoteTransportStartResult.Accepted
        } catch (_: Throwable) {
            lock.withLock {
                rtcSessions.remove(request.id, session)
                rtcRequests.remove(request.id)
            }
            session.close()
            RemoteTransportStartResult.Rejected(RemoteSessionFailure.TransportUnavailable)
        }
    }

    override suspend fun stop(sessionId: RemoteSessionId) {
        val rtc = lock.withLock {
            surfaces.remove(sessionId)
            nativeSessions.remove(sessionId)
            rtcRequests.remove(sessionId)
            rtcCapabilities.remove(sessionId)
            rtcSessions.remove(sessionId)
        }
        if (rtc != null) rtc.close() else native.stop(sessionId)
    }

    override suspend fun sendInput(sessionId: RemoteSessionId, command: InputCommand): Boolean {
        val rtc = lock.withLock { rtcSessions[sessionId] }
        return rtc?.sendInput(command) ?: native.sendInput(sessionId, command)
    }

    override suspend fun sendClipboardText(sessionId: RemoteSessionId, value: String): Boolean {
        val rtc = lock.withLock { rtcSessions[sessionId] }
        return rtc?.sendClipboardText(value) ?: native.sendClipboardText(sessionId, value)
    }

    override suspend fun sendClipboardFiles(
        sessionId: RemoteSessionId,
        generation: String,
        files: List<LocalClipboardFile>,
    ): Boolean = if (lock.withLock { rtcSessions.containsKey(sessionId) }) {
        false
    } else {
        native.sendClipboardFiles(sessionId, generation, files)
    }

    override suspend fun downloadClipboardFiles(
        sessionId: RemoteSessionId,
        generation: String,
        destinationDirectory: String,
    ): Boolean = if (lock.withLock { rtcSessions.containsKey(sessionId) }) {
        false
    } else {
        native.downloadClipboardFiles(sessionId, generation, destinationDirectory)
    }

    suspend fun switchMonitor(sessionId: RemoteSessionId, monitorName: String): Boolean {
        val rtc = lock.withLock { rtcSessions[sessionId] }
        return rtc?.switchMonitor(monitorName) ?: native.switchMonitor(sessionId, monitorName)
    }

    suspend fun requestVirtualDisplay(
        sessionId: RemoteSessionId,
        requestId: String,
        operation: RemoteVirtualDisplayOperation,
    ): Boolean = if (lock.withLock { rtcSessions.containsKey(sessionId) }) {
        false
    } else {
        native.requestVirtualDisplay(sessionId, requestId, operation)
    }

    suspend fun setAudioEnabled(sessionId: RemoteSessionId, enabled: Boolean): Boolean {
        val rtc = lock.withLock { rtcSessions[sessionId] }
        return rtc?.setAudioEnabled(enabled) ?: native.setAudioEnabled(sessionId, enabled)
    }

    override suspend fun listRemoteDirectory(sessionId: RemoteSessionId, path: String): Boolean =
        if (isRtcSession(sessionId)) false else native.listRemoteDirectory(sessionId, path)

    override suspend fun startUpload(sessionId: RemoteSessionId, localPath: String, remoteDirectory: String): Int? =
        if (isRtcSession(sessionId)) null else native.startUpload(sessionId, localPath, remoteDirectory)

    override suspend fun startDownload(sessionId: RemoteSessionId, remotePath: String, localDirectory: String): Int? =
        if (isRtcSession(sessionId)) null else native.startDownload(sessionId, remotePath, localDirectory)

    override suspend fun cancelTransfer(sessionId: RemoteSessionId, jobId: Int): Boolean =
        if (isRtcSession(sessionId)) false else native.cancelTransfer(sessionId, jobId)

    override suspend fun confirmOverwrite(
        sessionId: RemoteSessionId,
        jobId: Int,
        fileNumber: Int,
        overwrite: Boolean,
        offsetBytes: Long,
        applyToAll: Boolean,
    ): Boolean = if (isRtcSession(sessionId)) {
        false
    } else {
        native.confirmOverwrite(sessionId, jobId, fileNumber, overwrite, offsetBytes, applyToAll)
    }

    override suspend fun startRecording(
        sessionId: RemoteSessionId,
        recordingId: RecordingId,
        stagingDirectory: String,
    ): Boolean = if (isRtcSession(sessionId)) false else native.startRecording(sessionId, recordingId, stagingDirectory)

    override suspend fun stopRecording(sessionId: RemoteSessionId, recordingId: RecordingId): Boolean =
        if (isRtcSession(sessionId)) false else native.stopRecording(sessionId, recordingId)

    override suspend fun startVoiceCall(sessionId: RemoteSessionId): Boolean =
        if (isRtcSession(sessionId)) false else native.startVoiceCall(sessionId)

    override suspend fun stopVoiceCall(sessionId: RemoteSessionId): Boolean =
        if (isRtcSession(sessionId)) false else native.stopVoiceCall(sessionId)

    override suspend fun setVoiceMicrophoneMuted(sessionId: RemoteSessionId, muted: Boolean): Boolean =
        if (isRtcSession(sessionId)) false else native.setVoiceMicrophoneMuted(sessionId, muted)

    override suspend fun setVoiceSpeakerMuted(sessionId: RemoteSessionId, muted: Boolean): Boolean =
        if (isRtcSession(sessionId)) false else native.setVoiceSpeakerMuted(sessionId, muted)

    override fun close() {
        val sessions = kotlinx.coroutines.runBlocking {
            lock.withLock {
                rtcSessions.values.toList().also {
                    rtcSessions.clear()
                    rtcRequests.clear()
                    rtcCapabilities.clear()
                    nativeSessions.clear()
                    surfaces.clear()
                }
            }
        }
        sessions.forEach(WebRtcPeerSession::close)
        rtcRuntime?.close()
        rtcRuntime = null
        httpClient.dispatcher.executorService.shutdown()
        httpClient.connectionPool.evictAll()
    }

    private suspend fun handleRtcEvent(sessionId: RemoteSessionId, event: WebRtcPeerEvent) {
        val request = lock.withLock { rtcRequests[sessionId] } ?: return
        when (event) {
            WebRtcPeerEvent.Connected -> lock.withLock { rtcCapabilities[sessionId] }
                ?.let { mutableEvents.emit(RemoteTransportEvent.Connected(sessionId, it)) }
            WebRtcPeerEvent.Reconnecting -> mutableEvents.emit(RemoteTransportEvent.Reconnecting(sessionId, 1))
            is WebRtcPeerEvent.VideoSize -> mutableEvents.emit(
                RemoteTransportEvent.VideoSize(sessionId, RemoteVideoSize(event.width, event.height)),
            )

            is WebRtcPeerEvent.ServerConfiguration -> {
                val config = event.value
                val monitorNames = config.monitorsInfoList.map { it.name }.filter(String::isNotBlank).distinct()
                val ticket = (request.target as RemoteSessionTarget.Account).connectionTicket
                val capabilities = RemoteSessionCapabilities(
                    monitorNames = monitorNames,
                    activeMonitorName = config.capturingMonitorName,
                    supportsAudio = request.enableAudio && config.audioEnabled,
                    supportsInput = request.enableInput && config.canBeOperated && "input" in ticket.permissions,
                    supportsFileTransfer = false,
                    supportsClipboard = request.enableClipboard && "clipboard" in ticket.permissions,
                    supportsVirtualDisplays = false,
                    supportsVoiceCall = false,
                )
                lock.withLock { rtcCapabilities[sessionId] = capabilities }
                mutableEvents.emit(
                    RemoteTransportEvent.Connected(
                        sessionId,
                        capabilities,
                    ),
                )
            }

            is WebRtcPeerEvent.ClipboardText -> mutableEvents.emit(RemoteTransportEvent.ClipboardText(sessionId, event.value))
            is WebRtcPeerEvent.GamepadRumble -> mutableEvents.emit(
                RemoteTransportEvent.GamepadRumble(sessionId, event.strongMotor, event.weakMotor),
            )

            is WebRtcPeerEvent.Closed -> {
                lock.withLock {
                    rtcSessions.remove(sessionId)
                    rtcRequests.remove(sessionId)
                    rtcCapabilities.remove(sessionId)
                }
                mutableEvents.emit(
                    RemoteTransportEvent.Disconnected(
                        sessionId,
                        RemoteSessionFailure.NetworkUnavailable,
                        event.recoverable,
                    ),
                )
            }
        }
    }

    private suspend fun isRtcSession(sessionId: RemoteSessionId): Boolean = lock.withLock { rtcSessions.containsKey(sessionId) }
}

internal data class StandardRtcLaunch(
    val parameters: StandardRtcSignalParameters,
    val iceConfiguration: RtcIceConfiguration,
    val permissions: Set<String>,
)

internal fun RemoteSessionRequest.standardRtcLaunchOrNull(nowEpochSeconds: Long = System.currentTimeMillis() / 1000L): StandardRtcLaunch? {
    val account = target as? RemoteSessionTarget.Account ?: return null
    val ticket = account.connectionTicket
    val ice = parseRtcIceConfiguration(ticket.rtcIceConfigJson) ?: return null
    if (ice.expiresAtEpochSeconds <= nowEpochSeconds + RTC_EXPIRY_MARGIN_SECONDS) return null
    if (ticket.expiresAtEpochMillis <= (nowEpochSeconds + RTC_EXPIRY_MARGIN_SECONDS) * 1000L) return null
    if (ticket.ticket.isBlank() || ticket.streamId.isBlank() || account.clientNonce.isBlank() || account.fallbackRemoteDeviceId.isBlank()) return null
    if (ticket.relayHost.isBlank() || ticket.relayPort !in 1..65535) return null
    if ("view" !in ticket.permissions) return null
    if (!ticket.signalDeviceId.startsWith("server_") || ticket.signalDeviceId.length <= "server_".length) return null
    val uri = runCatching { URI(ticket.launchUrl) }.getOrNull() ?: return null
    if (uri.scheme?.lowercase() !in setOf("http", "https")) return null
    val instanceId = uri.fragmentParameterForRtc("instance").orEmpty()
    return StandardRtcLaunch(
        parameters = StandardRtcSignalParameters(
            relayHost = ticket.relayHost,
            relayPort = ticket.relayPort,
            secure = uri.scheme.equals("https", ignoreCase = true),
            remoteDeviceId = ticket.signalDeviceId,
            ticketDeviceId = account.fallbackRemoteDeviceId,
            streamId = ticket.streamId,
            ticket = ticket.ticket,
            clientNonce = account.clientNonce,
            instanceId = instanceId,
        ),
        iceConfiguration = ice,
        permissions = ticket.permissions,
    )
}

private fun URI.fragmentParameterForRtc(name: String): String? = rawFragment
    ?.split('&')
    ?.asSequence()
    ?.map { component -> component.substringBefore('=') to component.substringAfter('=', "") }
    ?.firstOrNull { (key) -> URLDecoder.decode(key, "UTF-8") == name }
    ?.second
    ?.let { URLDecoder.decode(it, "UTF-8") }

private const val RTC_EXPIRY_MARGIN_SECONDS = 15L
