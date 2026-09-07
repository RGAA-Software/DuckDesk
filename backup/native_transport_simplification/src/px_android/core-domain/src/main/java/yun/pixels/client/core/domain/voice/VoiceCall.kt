package yun.pixels.client.core.domain.voice

import kotlinx.coroutines.flow.Flow
import yun.pixels.client.core.domain.session.RemoteSessionId

enum class VoiceCallPhase {
    Idle,
    Requesting,
    Connected,
}

data class VoiceCallState(
    val phase: VoiceCallPhase = VoiceCallPhase.Idle,
    val microphoneMuted: Boolean = false,
    val speakerMuted: Boolean = false,
    val speakerphone: Boolean = false,
    val requiresHeadset: Boolean = true,
    val reason: String = "",
)

data class VoiceCallEvent(
    val sessionId: RemoteSessionId,
    val state: VoiceCallState,
)

interface VoiceCallTransport {
    val voiceCallEvents: Flow<VoiceCallEvent>

    suspend fun startVoiceCall(sessionId: RemoteSessionId): Boolean

    suspend fun stopVoiceCall(sessionId: RemoteSessionId): Boolean

    suspend fun setVoiceMicrophoneMuted(sessionId: RemoteSessionId, muted: Boolean): Boolean

    suspend fun setVoiceSpeakerMuted(sessionId: RemoteSessionId, muted: Boolean): Boolean
}
