package yun.pixels.client.core.nativebridge

import android.os.SystemClock
import java.io.Closeable
import java.util.UUID
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.webrtc.AudioSource
import org.webrtc.AudioTrack
import org.webrtc.MediaConstraints
import org.webrtc.PeerConnectionFactory
import org.webrtc.RtpTransceiver
import px.PxMessage
import yun.pixels.client.core.domain.voice.VoiceCallPhase
import yun.pixels.client.core.domain.voice.VoiceCallState

/** Owns the standard-WebRTC call control state and the dedicated microphone/
 * voice tracks. The microphone track is created only after a user action and
 * is attached to the pre-negotiated sender only after the remote user accepts. */
internal class RtcVoiceCallController(
    private val factory: PeerConnectionFactory,
    private val scope: CoroutineScope,
    private val enabled: Boolean,
    private val sendMessage: (PxMessage.Message.Builder) -> Boolean,
    private val onState: (VoiceCallState) -> Unit,
) : Closeable {
    private val lock = Any()
    private var closed = false
    private var transceiver: RtpTransceiver? = null
    private var remoteTrack: AudioTrack? = null
    private var localSource: AudioSource? = null
    private var localTrack: AudioTrack? = null
    private var supported = false
    private var requiresHeadset = true
    private var callId = ""
    private var requestId = 0L
    private var state = VoiceCallState()
    private var timeoutJob: Job? = null

    private data class Release(
        val callId: String,
        val requestId: Long,
        val source: AudioSource?,
        val track: AudioTrack?,
        val timeoutJob: Job?,
        val state: VoiceCallState,
    )

    fun attachTransceiver(value: RtpTransceiver) {
        synchronized(lock) {
            check(!closed) { "RTC voice controller is closed" }
            transceiver = value
        }
    }

    fun isVoiceTrack(track: AudioTrack): Boolean = track.id() == VOICE_REMOTE_TRACK_ID

    fun onRemoteTrack(track: AudioTrack) {
        val previous = synchronized(lock) {
            if (closed) {
                track.setEnabled(false)
                return
            }
            val old = remoteTrack
            remoteTrack = track
            track.setEnabled(state.phase == VoiceCallPhase.Connected && !state.speakerMuted)
            old
        }
        if (previous !== track) previous?.setEnabled(false)
    }

    fun updateConfiguration(configuration: PxMessage.ServerConfiguration) {
        val available = enabled && configuration.voiceCallEnabled &&
            configuration.voiceCallProtocolVersion >= MIN_VOICE_CALL_PROTOCOL_VERSION
        val activeCallId = synchronized(lock) {
            supported = available && transceiver != null && !closed
            requiresHeadset = configuration.voiceCallRequiresHeadset
            if (state.phase == VoiceCallPhase.Idle) {
                state = VoiceCallState(requiresHeadset = requiresHeadset)
                null
            } else if (!supported) {
                callId
            } else {
                state = state.copy(requiresHeadset = requiresHeadset)
                null
            }
        }
        if (activeCallId != null) finish(activeCallId, notifyRemote = true, reason = "unsupported")
    }

    fun handleMessage(message: PxMessage.Message): Boolean = when (message.type) {
        PxMessage.MessageType.kVoiceCallResponse -> {
            handleResponse(message)
            true
        }
        PxMessage.MessageType.kVoiceCallRequest -> {
            handleRequest(message)
            true
        }
        PxMessage.MessageType.kVoiceAudioConfig -> {
            handleAudioConfig(message)
            true
        }
        else -> false
    }

    fun start(): Boolean {
        val source = synchronized(lock) {
            if (closed || !supported || transceiver == null || state.phase != VoiceCallPhase.Idle) return false
            factory.createAudioSource(voiceAudioConstraints())
        }
        val track = factory.createAudioTrack(VOICE_MICROPHONE_TRACK_ID, source)
        track.setEnabled(true)
        val identity = synchronized(lock) {
            if (closed || !supported || transceiver == null || state.phase != VoiceCallPhase.Idle) {
                null
            } else {
                val nextCallId = UUID.randomUUID().toString()
                val nextRequestId = SystemClock.elapsedRealtimeNanos().coerceAtLeast(1L)
                localSource = source
                localTrack = track
                callId = nextCallId
                requestId = nextRequestId
                state = VoiceCallState(phase = VoiceCallPhase.Requesting, requiresHeadset = requiresHeadset)
                nextCallId to nextRequestId
            }
        }
        if (identity == null) {
            track.dispose()
            source.dispose()
            return false
        }
        val pendingTimeout = scope.launch {
            delay(VOICE_CALL_TIMEOUT_MILLIS)
            finish(identity.first, notifyRemote = true, reason = "timeout")
        }
        synchronized(lock) {
            if (callId == identity.first && requestId == identity.second) timeoutJob = pendingTimeout else pendingTimeout.cancel()
        }
        if (!sendRequest(identity.first, identity.second, connect = true)) {
            finish(identity.first, notifyRemote = false, reason = "control_channel_unavailable")
            return false
        }
        publishState()
        return true
    }

    fun stop(): Boolean = finish(expectedCallId = null, notifyRemote = true, reason = "local_hangup")

    fun setMicrophoneMuted(muted: Boolean): Boolean {
        val updated = synchronized(lock) {
            val microphone = localTrack
            if (closed || state.phase != VoiceCallPhase.Connected || microphone == null) return false
            microphone.setEnabled(!muted)
            state = state.copy(microphoneMuted = muted)
            state
        }
        onState(updated)
        return true
    }

    fun setSpeakerMuted(muted: Boolean): Boolean {
        val updated = synchronized(lock) {
            if (closed || state.phase != VoiceCallPhase.Connected) return false
            remoteTrack?.setEnabled(!muted)
            state = state.copy(speakerMuted = muted)
            state
        }
        onState(updated)
        return true
    }

    override fun close() {
        synchronized(lock) {
            if (closed) return
            closed = true
        }
        finish(expectedCallId = null, notifyRemote = false, reason = "session_ended")
        synchronized(lock) {
            remoteTrack?.setEnabled(false)
            remoteTrack = null
            transceiver = null
            supported = false
        }
    }

    private fun handleResponse(message: PxMessage.Message) {
        val identity = synchronized(lock) { callId to requestId }
        if (!message.isExpectedRtcVoiceCallResponse(identity.first, identity.second)) return
        val response = message.voiceCallResponse
        val rejectedReason = synchronized(lock) {
            if (state.phase != VoiceCallPhase.Requesting || response.callId != callId || response.requestId != requestId) return
            if (!response.accepted) response.reason.ifBlank { "rejected" }.take(MAX_RTC_VOICE_REASON_CHARS) else null
        }
        if (rejectedReason != null) {
            finish(response.callId, notifyRemote = false, reason = rejectedReason)
            return
        }
        val connected = synchronized(lock) {
            if (state.phase != VoiceCallPhase.Requesting || response.callId != callId || response.requestId != requestId) return
            val sender = transceiver?.sender ?: return@synchronized null
            val microphone = localTrack ?: return@synchronized null
            if (!sender.setTrack(microphone, false)) return@synchronized null
            timeoutJob?.cancel()
            timeoutJob = null
            state = state.copy(phase = VoiceCallPhase.Connected, reason = "")
            remoteTrack?.setEnabled(!state.speakerMuted)
            state
        }
        if (connected == null) {
            finish(response.callId, notifyRemote = true, reason = "media_attach_failed")
        } else {
            onState(connected)
        }
    }

    private fun handleRequest(message: PxMessage.Message) {
        if (!message.hasVoiceCallRequest()) return
        val request = message.voiceCallRequest
        if (!isValidRtcVoiceCallIdentity(request.callId, request.requestId)) return
        if (!request.connect) {
            val matches = synchronized(lock) { message.isMatchingRtcVoiceHangup(callId) }
            if (matches) finish(request.callId, notifyRemote = false, reason = "remote_hangup")
            return
        }
        sendMessage(
            PxMessage.Message.newBuilder()
                .setType(PxMessage.MessageType.kVoiceCallResponse)
                .setVoiceCallResponse(
                    PxMessage.VoiceCallResponse.newBuilder()
                        .setCallId(request.callId)
                        .setRequestId(request.requestId)
                        .setAccepted(false)
                        .setReason("unsupported_direction"),
                ),
        )
    }

    private fun handleAudioConfig(message: PxMessage.Message) {
        val incompatible = synchronized(lock) {
            state.phase == VoiceCallPhase.Connected && message.hasIncompatibleRtcVoiceAudioConfig(callId)
        }
        if (incompatible) finish(message.voiceAudioConfig.callId, notifyRemote = true, reason = "incompatible_audio_config")
    }

    private fun sendRequest(callId: String, requestId: Long, connect: Boolean): Boolean = sendMessage(
        PxMessage.Message.newBuilder()
            .setType(PxMessage.MessageType.kVoiceCallRequest)
            .setVoiceCallRequest(
                PxMessage.VoiceCallRequest.newBuilder()
                    .setCallId(callId)
                    .setRequestId(requestId)
                    .setConnect(connect),
            ),
    )

    private fun finish(expectedCallId: String?, notifyRemote: Boolean, reason: String): Boolean {
        val release = synchronized(lock) {
            if (state.phase == VoiceCallPhase.Idle || (expectedCallId != null && expectedCallId != callId)) return false
            transceiver?.sender?.setTrack(null, false)
            remoteTrack?.setEnabled(false)
            Release(
                callId = callId,
                requestId = requestId,
                source = localSource,
                track = localTrack,
                timeoutJob = timeoutJob,
                state = VoiceCallState(requiresHeadset = requiresHeadset, reason = reason),
            ).also {
                callId = ""
                requestId = 0L
                localSource = null
                localTrack = null
                timeoutJob = null
                state = it.state
            }
        }
        release.timeoutJob?.cancel()
        release.track?.setEnabled(false)
        release.track?.dispose()
        release.source?.dispose()
        if (notifyRemote && release.callId.isNotEmpty() && release.requestId != 0L) {
            sendRequest(release.callId, release.requestId, connect = false)
        }
        onState(release.state)
        return true
    }

    private fun publishState() {
        val snapshot = synchronized(lock) { state }
        onState(snapshot)
    }
}

private fun voiceAudioConstraints(): MediaConstraints = MediaConstraints().apply {
    mandatory += MediaConstraints.KeyValuePair("googEchoCancellation", "true")
    mandatory += MediaConstraints.KeyValuePair("googNoiseSuppression", "true")
    mandatory += MediaConstraints.KeyValuePair("googAutoGainControl", "true")
}

private const val VOICE_MICROPHONE_TRACK_ID = "pixels_android_voice_microphone"
private const val VOICE_REMOTE_TRACK_ID = "voice_call_audio"
private const val MIN_VOICE_CALL_PROTOCOL_VERSION = 1
private const val VOICE_CALL_TIMEOUT_MILLIS = 30_500L
