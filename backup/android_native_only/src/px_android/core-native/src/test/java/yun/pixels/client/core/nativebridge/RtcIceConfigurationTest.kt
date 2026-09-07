package yun.pixels.client.core.nativebridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RtcIceConfigurationTest {
    @Test
    fun parsesConsoleSessionIceConfiguration() {
        val result = parseRtcIceConfiguration(
            """
            {
              "revision": 19,
              "direct_probe_enabled": true,
              "expires_at": 1893456000,
              "ice_servers": [
                {
                  "id": "turn-primary",
                  "urls": ["turn:relay.example.com:3478?transport=udp", "turns:relay.example.com:5349"],
                  "username": "ticket-user",
                  "credential": "ticket-secret"
                },
                {"id": "stun", "urls": ["stun:relay.example.com:3478"]}
              ]
            }
            """.trimIndent(),
        )

        requireNotNull(result)
        assertEquals(19L, result.revision)
        assertTrue(result.directProbeEnabled)
        assertEquals(1_893_456_000L, result.expiresAtEpochSeconds)
        assertEquals(2, result.servers.size)
        assertEquals("ticket-user", result.servers.first().username)
        assertEquals("", result.servers.last().credential)
    }

    @Test
    fun rejectsMissingExpiryEmptyServersAndUnsupportedUrls() {
        assertNull(parseRtcIceConfiguration("""{"revision":1,"ice_servers":[]}"""))
        assertNull(parseRtcIceConfiguration("""{"revision":1,"expires_at":10,"ice_servers":[]}"""))
        assertNull(
            parseRtcIceConfiguration(
                """{"revision":1,"expires_at":10,"ice_servers":[{"id":"bad","urls":["https://relay.example.com"]}]}""",
            ),
        )
    }

    @Test
    fun directProbeDefaultsToDisabled() {
        val result = parseRtcIceConfiguration(
            """{"revision":1,"expires_at":10,"ice_servers":[{"id":"stun","urls":["stun:relay.example.com"]}]}""",
        )

        requireNotNull(result)
        assertFalse(result.directProbeEnabled)
    }
}
