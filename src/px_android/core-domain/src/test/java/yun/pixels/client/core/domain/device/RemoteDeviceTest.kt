package yun.pixels.client.core.domain.device

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class RemoteDeviceTest {
    @Test
    fun deviceIdRejectsBlankValues() {
        assertThrows(IllegalArgumentException::class.java) {
            DeviceId("  ")
        }
    }

    @Test
    fun remoteDeviceKeepsDeterministicDefaults() {
        val device = RemoteDevice(
            id = DeviceId("office-pc"),
            displayName = "Office PC",
            platformName = "Windows",
            availability = DeviceAvailability.Offline,
            endpoint = DeviceEndpoint("192.168.1.8"),
        )

        assertEquals(null, device.latencyMillis)
        assertEquals(null, device.lastConnectedEpochMillis)
    }
}
