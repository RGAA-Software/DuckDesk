package yun.pixels.client.core.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import yun.pixels.client.core.domain.session.RemoteInputMode

class RemoteSessionPreferencesCodecTest {
    @Test
    fun missingOrInvalidStorageFallsBackToProductDefaults() {
        val decoded = decodeRemoteSessionPreferences(frameRate = 45, audioEnabled = null, inputMode = "removed-mode")

        assertEquals(60, decoded.frameRate)
        assertEquals(true, decoded.audioEnabled)
        assertEquals(RemoteInputMode.DirectTouch, decoded.inputMode)
    }

    @Test
    fun validStoredValuesAreRestored() {
        val decoded = decodeRemoteSessionPreferences(frameRate = 30, audioEnabled = false, inputMode = RemoteInputMode.Touchpad.name)

        assertEquals(30, decoded.frameRate)
        assertFalse(decoded.audioEnabled)
        assertEquals(RemoteInputMode.Touchpad, decoded.inputMode)
    }
}
