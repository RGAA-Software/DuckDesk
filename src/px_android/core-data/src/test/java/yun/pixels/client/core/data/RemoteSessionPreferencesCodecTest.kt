package yun.pixels.client.core.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import yun.pixels.client.core.domain.session.RemoteInputMode
import yun.pixels.client.core.domain.session.RemoteDecoderMode
import yun.pixels.client.core.domain.session.RemoteConnectionRoute

class RemoteSessionPreferencesCodecTest {
    @Test
    fun missingOrInvalidStorageFallsBackToProductDefaults() {
        val decoded = decodeRemoteSessionPreferences(
            frameRate = 45,
            audioEnabled = null,
            inputMode = "removed-mode",
            decoderMode = "removed-decoder",
            connectionRoute = "removed-route",
        )

        assertEquals(60, decoded.frameRate)
        assertEquals(true, decoded.audioEnabled)
        assertEquals(RemoteInputMode.DirectTouch, decoded.inputMode)
        assertEquals(RemoteDecoderMode.Automatic, decoded.decoderMode)
        assertEquals(RemoteConnectionRoute.Automatic, decoded.connectionRoute)
    }

    @Test
    fun validStoredValuesAreRestored() {
        val decoded = decodeRemoteSessionPreferences(
            frameRate = 30,
            audioEnabled = false,
            inputMode = RemoteInputMode.Touchpad.name,
            decoderMode = RemoteDecoderMode.Software.name,
            connectionRoute = RemoteConnectionRoute.Relay.name,
        )

        assertEquals(30, decoded.frameRate)
        assertFalse(decoded.audioEnabled)
        assertEquals(RemoteInputMode.Touchpad, decoded.inputMode)
        assertEquals(RemoteDecoderMode.Software, decoded.decoderMode)
        assertEquals(RemoteConnectionRoute.Relay, decoded.connectionRoute)
    }
}
