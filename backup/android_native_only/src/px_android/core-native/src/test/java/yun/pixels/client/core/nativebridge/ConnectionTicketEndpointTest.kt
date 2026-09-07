package yun.pixels.client.core.nativebridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.JoinMode

class ConnectionTicketEndpointTest {
    @Test
    fun acceptsPrivateCleartextLaunchAndUsesBareDeviceId() {
        val endpoint = ticket("http://192.168.31.6:20371/web_client/?deviceId=device-42").toNativeEndpoint("fallback")

        requireNotNull(endpoint)
        assertEquals("192.168.31.6", endpoint.host)
        assertEquals(20371, endpoint.port)
        assertEquals(false, endpoint.ssl)
        assertEquals("device-42", endpoint.remoteDeviceId)
    }

    @Test
    fun rejectsPublicCleartextAndUnknownSchemes() {
        assertNull(ticket("http://203.0.113.5:20371/web_client/?deviceId=device-42").toNativeEndpoint("fallback"))
        assertNull(ticket("ftp://192.168.31.6/file").toNativeEndpoint("fallback"))
    }

    @Test
    fun acceptsTlsHostAndFallsBackToAccountDeviceId() {
        val endpoint = ticket("https://edge.example.com/web_client/").toNativeEndpoint("account-device")

        requireNotNull(endpoint)
        assertEquals("edge.example.com", endpoint.host)
        assertEquals(443, endpoint.port)
        assertEquals(true, endpoint.ssl)
        assertEquals("account-device", endpoint.remoteDeviceId)
    }

    @Test
    fun selectsRelayForPublicAccountEndpointWithBoundRelayTarget() {
        val ticket = ticket("https://edge.example.com/web_client/").copy(
            relayHost = "relay.example.com",
            relayPort = 443,
        )
        val endpoint = requireNotNull(ticket.toNativeEndpoint("account-device"))

        assertEquals(NATIVE_NETWORK_TYPE_RELAY, selectNativeNetworkType(endpoint, ticket))
    }

    @Test
    fun keepsUdpDirectForPrivateAccountEndpoint() {
        val ticket = ticket("http://192.168.31.6:20371/web_client/?deviceId=device-42").copy(
            relayHost = "relay.example.com",
            relayPort = 443,
        )
        val endpoint = requireNotNull(ticket.toNativeEndpoint("account-device"))

        assertEquals(NATIVE_NETWORK_TYPE_UDP_DIRECT, selectNativeNetworkType(endpoint, ticket))
    }

    private fun ticket(launchUrl: String) = ConnectionTicket(
        ticket = "ticket",
        renewalToken = "renewal",
        launchUrl = launchUrl,
        expiresAtEpochMillis = Long.MAX_VALUE,
        logicalSessionId = "session",
        streamId = "stream",
        joinMode = JoinMode.Control,
        permissions = setOf("view"),
        rtcIceConfigJson = "",
        relayHost = "",
        relayPort = 0,
        signalDeviceId = "server_device-42",
    )
}
