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
    fun nativeConfigIgnoresRelayAndIceInstructions() {
        val targetTicket = ticket("https://edge.example.com/web_client/?deviceId=device-42#instance=instance-7").copy(
            relayHost = "relay.example.com",
            relayPort = 443,
            rtcIceConfigJson = """{"ice_servers":[{"urls":["turn:relay.example.com"]}]}""",
        )
        val request = yun.pixels.client.core.domain.session.RemoteSessionRequest(
            id = yun.pixels.client.core.domain.session.RemoteSessionId("session"),
            target = yun.pixels.client.core.domain.session.RemoteSessionTarget.Account(
                displayName = "Desktop",
                fallbackRemoteDeviceId = "device-42",
                connectionTicket = targetTicket,
                clientNonce = "nonce",
            ),
        )
        val config = requireNotNull(request.toNativeConfig("client-device", null))
        assertEquals("edge.example.com", config.host)
        assertEquals(443, config.port)
        assertEquals("ticket", config.connectionTicket)
        assertEquals("nonce", config.connectionNonce)
        assertEquals("instance-7", config.connectionInstanceId)
        org.junit.Assert.assertFalse(config.enableInput)
        org.junit.Assert.assertFalse(config.enableAudio)
        org.junit.Assert.assertFalse(config.enableClipboard)
        val account = request.target as yun.pixels.client.core.domain.session.RemoteSessionTarget.Account
        assertNull(request.copy(target = account.copy(connectionTicket = targetTicket.copy(permissions = setOf("input"))))
            .toNativeConfig("client-device", null))
        assertNull(request.copy(target = account.copy(clientNonce = "")).toNativeConfig("client-device", null))
        // The platform contract has no protocol selector, Relay endpoint or ICE configuration.
        org.junit.Assert.assertFalse(NativeSessionConfig::class.java.declaredFields.any {
            it.name in setOf("networkType", "relayHost", "relayPort", "rtcIceConfigJson")
        })
    }

    @Test
    fun renewsExpiringOrPreviouslyAttemptedTicket() {
        org.junit.Assert.assertTrue(ConnectionTicketAttempt(ticket("https://edge.example.com").copy(expiresAtEpochMillis = 1_010_000L))
            .requiresRenewal(1_000_000L))
        val attempt = ConnectionTicketAttempt(ticket("https://edge.example.com"))
        org.junit.Assert.assertFalse(attempt.requiresRenewal(1_000_000L))
        attempt.markAttempted()
        org.junit.Assert.assertTrue(attempt.requiresRenewal(1_000_000L))
    }

    @Test
    fun repeatedRejectedConnectionsRenewFromTheLatestRotatingCapability() {
        val initial = ticket("https://edge.example.com")
        val attempt = ConnectionTicketAttempt(initial)
        repeat(10) { round ->
            attempt.markAttempted() // Includes admission rejection after ticket redemption.
            org.junit.Assert.assertTrue(attempt.requiresRenewal(1_000_000L))
            assertEquals(if (round == 0) "renewal" else "renewal-$round", attempt.current.renewalToken)
            val next = initial.copy(ticket = "ticket-${round + 1}", renewalToken = "renewal-${round + 1}")
            attempt.renewed(next)
            assertEquals(next, attempt.current)
            org.junit.Assert.assertFalse(attempt.requiresRenewal(1_000_000L))
        }
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
