package yun.pixels.client.core.domain.voice

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class VoiceCallTest {
    @Test
    fun defaultsAreSafeAndIdle() {
        val state = VoiceCallState()

        assertTrue(state.phase == VoiceCallPhase.Idle)
        assertFalse(state.microphoneMuted)
        assertFalse(state.speakerMuted)
        assertTrue(state.requiresHeadset)
    }

    @Test
    fun connectedControlsRemainTyped() {
        val state = VoiceCallState(
            phase = VoiceCallPhase.Connected,
            microphoneMuted = true,
            speakerMuted = true,
            requiresHeadset = false,
        )

        assertTrue(state.microphoneMuted)
        assertTrue(state.speakerMuted)
    }
}
