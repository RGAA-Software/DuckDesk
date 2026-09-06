package yun.pixels.client.core.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AndroidLanDeviceDiscoveryTest {
    @Test
    fun widerNetworkIsBoundedToLocalSlash24() {
        val candidates = subnetCandidates(byteArrayOf(10, 20, 30, 40), 16)

        assertEquals(253, candidates.size)
        assertEquals("10.20.30.1", candidates.first())
        assertEquals("10.20.30.254", candidates.last())
        assertFalse("10.20.30.40" in candidates)
    }

    @Test
    fun narrowNetworkUsesItsRealPrefix() {
        val candidates = subnetCandidates(byteArrayOf(192.toByte(), 168.toByte(), 8, 6), 29)

        assertEquals(listOf("192.168.8.1", "192.168.8.2", "192.168.8.3", "192.168.8.4", "192.168.8.5"), candidates)
        assertTrue("192.168.8.6" !in candidates)
    }
}
