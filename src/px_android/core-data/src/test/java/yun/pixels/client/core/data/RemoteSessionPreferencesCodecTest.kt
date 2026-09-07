package yun.pixels.client.core.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import yun.pixels.client.core.domain.session.RemoteInputMode
import yun.pixels.client.core.domain.session.RemoteDecoderMode

class RemoteSessionPreferencesCodecTest {
    @Test
    fun missingOrInvalidStorageFallsBackToProductDefaults() {
        val decoded = decodeRemoteSessionPreferences(
            frameRate = 45,
            audioEnabled = null,
            inputMode = "removed-mode",
            decoderMode = "removed-decoder",
        )

        assertEquals(60, decoded.frameRate)
        assertEquals(true, decoded.audioEnabled)
        assertEquals(RemoteInputMode.DirectTouch, decoded.inputMode)
        assertEquals(RemoteDecoderMode.Automatic, decoded.decoderMode)
    }

    @Test
    fun validStoredValuesAreRestored() {
        val decoded = decodeRemoteSessionPreferences(
            frameRate = 30,
            audioEnabled = false,
            inputMode = RemoteInputMode.Touchpad.name,
            decoderMode = RemoteDecoderMode.Software.name,
        )

        assertEquals(30, decoded.frameRate)
        assertFalse(decoded.audioEnabled)
        assertEquals(RemoteInputMode.Touchpad, decoded.inputMode)
        assertEquals(RemoteDecoderMode.Software, decoded.decoderMode)
    }
}
