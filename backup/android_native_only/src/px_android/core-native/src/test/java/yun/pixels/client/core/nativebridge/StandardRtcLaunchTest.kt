package yun.pixels.client.core.nativebridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.JoinMode
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionTarget

class StandardRtcLaunchTest {
    @Test
    fun selectsStandardRtcOnlyForCompleteUnexpiredAccountTicket() {
        val launch = request(ticket()).standardRtcLaunchOrNull(nowEpochSeconds = 1_000L)

        requireNotNull(launch)
        assertEquals("relay.example.com", launch.parameters.relayHost)
        assertEquals("server_device-7__instance__instance-4", launch.parameters.remoteDeviceId)
        assertEquals("device-7", launch.parameters.ticketDeviceId)
        assertEquals("instance-4", launch.parameters.instanceId)
        assertTrue(launch.parameters.secure)
        assertTrue("input" in launch.permissions)
    }

    @Test
    fun fallsBackToNativeTransportWhenIceOrTicketIsExpiring() {
        assertNull(request(ticket(iceExpiry = 1_010L)).standardRtcLaunchOrNull(nowEpochSeconds = 1_000L))
        assertNull(request(ticket(ticketExpiryMillis = 1_010_000L)).standardRtcLaunchOrNull(nowEpochSeconds = 1_000L))
        assertNull(request(ticket(iceJson = "{}")).standardRtcLaunchOrNull(nowEpochSeconds = 1_000L))
    }

    @Test
    fun doesNotSelectRtcForMalformedRelayTarget() {
        val launch = request(ticket(signalDeviceId = "device-7")).standardRtcLaunchOrNull(nowEpochSeconds = 1_000L)

        assertNull(launch)
        assertFalse(ticket().permissions.isEmpty())
    }

    @Test
    fun requiresViewPermissionBeforeSelectingRtc() {
        assertNull(request(ticket(permissions = setOf("input"))).standardRtcLaunchOrNull(nowEpochSeconds = 1_000L))
    }

    @Test
    fun renewsAnExpiringOrAlreadyAttemptedOneTimeTicket() {
        assertTrue(ticket(ticketExpiryMillis = 1_010_000L).requiresRenewal(null, nowEpochMillis = 1_000_000L))
        assertTrue(ticket().requiresRenewal("ticket", nowEpochMillis = 1_000_000L))
        assertFalse(ticket().requiresRenewal("different-ticket", nowEpochMillis = 1_000_000L))
    }

    private fun request(ticket: ConnectionTicket) = RemoteSessionRequest(
        id = RemoteSessionId("session-1"),
        target = RemoteSessionTarget.Account(
            displayName = "Desktop",
            fallbackRemoteDeviceId = "device-7",
            connectionTicket = ticket,
            clientNonce = "nonce-8",
        ),
    )

    private fun ticket(
        iceExpiry: Long = 2_000L,
        ticketExpiryMillis: Long = 2_000_000L,
        signalDeviceId: String = "server_device-7__instance__instance-4",
        permissions: Set<String> = setOf("view", "input", "clipboard"),
        iceJson: String = """
            {
              "revision": 2,
              "direct_probe_enabled": true,
              "expires_at": $iceExpiry,
              "ice_servers": [{"id":"turn","urls":["turn:relay.example.com:3478"],"username":"u","credential":"p"}]
            }
        """.trimIndent(),
    ) = ConnectionTicket(
        ticket = "ticket",
        renewalToken = "renewal",
        launchUrl = "https://console.example.com/web_client/?deviceId=device-7#instance=instance-4",
        expiresAtEpochMillis = ticketExpiryMillis,
        logicalSessionId = "logical-1",
        streamId = "stream-2",
        joinMode = JoinMode.Control,
        permissions = permissions,
        rtcIceConfigJson = iceJson,
        relayHost = "relay.example.com",
        relayPort = 443,
        signalDeviceId = signalDeviceId,
    )
}
