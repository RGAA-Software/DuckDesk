package yun.pixels.client.core.domain.session

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Test
import yun.pixels.client.core.domain.device.DeviceAvailability
import yun.pixels.client.core.domain.device.DeviceEndpoint
import yun.pixels.client.core.domain.device.DeviceId
import yun.pixels.client.core.domain.device.RemoteDevice

class RemoteSessionPreferencesTest {
    @Test
    fun defaultsDescribeAFullQualityInteractiveSession() {
        val preferences = RemoteSessionPreferences()

        assertEquals(60, preferences.frameRate)
        assertEquals(RemoteInputMode.DirectTouch, preferences.inputMode)
        assertEquals(true, preferences.audioEnabled)
        assertEquals(RemoteDecoderMode.Automatic, preferences.decoderMode)
    }

    @Test
    fun rejectsFrameRatesThatTheProductDoesNotOffer() {
        assertThrows(IllegalArgumentException::class.java) { RemoteSessionPreferences(frameRate = 45) }
    }

    @Test
    fun playoutPreferenceDoesNotDisableAudioTrackNegotiation() {
        val request = RemoteSessionRequest(
            id = RemoteSessionId("session"),
            target = RemoteSessionTarget.Direct(device(), null),
            preferences = RemoteSessionPreferences(audioEnabled = false),
        )

        assertEquals(true, request.enableAudio)
        assertFalse(request.preferences.audioEnabled)
        assertEquals("direct:desktop-1", request.target.preferenceKey)
    }

    private fun device() = RemoteDevice(
        id = DeviceId("desktop-1"),
        displayName = "Desktop",
        platformName = "Windows",
        availability = DeviceAvailability.Online,
        endpoint = DeviceEndpoint("192.168.1.2"),
    )
}
