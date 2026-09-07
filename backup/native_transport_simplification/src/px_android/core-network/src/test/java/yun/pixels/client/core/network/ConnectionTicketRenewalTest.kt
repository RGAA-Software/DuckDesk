package yun.pixels.client.core.network

import org.json.JSONObject
import org.json.JSONArray
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.JoinMode

class ConnectionTicketRenewalTest {
    @Test
    fun `renewal rotates credentials and preserves launch routing`() {
        val result = parseTicket(renewalResponse(), originalTicket())

        require(result is AccountResult.Success)
        assertEquals("ticket-2", result.value.ticket)
        assertEquals("renewal-2", result.value.renewalToken)
        assertEquals("https://console.example/web_client/#instance=i-1", result.value.launchUrl)
        assertEquals("relay.example", result.value.relayHost)
        assertEquals(443, result.value.relayPort)
        assertEquals("server_device-1__instance__i-1", result.value.signalDeviceId)
        assertTrue("view" in result.value.permissions)
        assertTrue(result.value.rtcIceConfigJson.contains("turns:relay.example:5349"))
    }

    @Test
    fun `renewal rejects changed logical session or stream`() {
        val changedStream = renewalResponse().put("stream_id", "other-stream")
        val changedSession = renewalResponse().put("logical_session_id", "other-session")

        assertEquals(AccountResult.Failure(AccountFailure.InvalidResponse), parseTicket(changedStream, originalTicket()))
        assertEquals(AccountResult.Failure(AccountFailure.InvalidResponse), parseTicket(changedSession, originalTicket()))
    }

    private fun renewalResponse() = JSONObject()
        .put("ticket", "ticket-2")
        .put("renewal_token", "renewal-2")
        .put("expires_at", 2_000_000L)
        .put("logical_session_id", "logical-1")
        .put("stream_id", "stream-1")
        .put("join_mode", "control")
        .put("permissions", JSONArray().put("view").put("input"))
        .put(
            "rtc_ice_config",
            JSONObject()
                .put("revision", 2)
                .put("expires_at", 2_000)
                .put(
                    "ice_servers",
                    JSONArray().put(
                        JSONObject()
                            .put("id", "turn")
                            .put("urls", JSONArray().put("turns:relay.example:5349")),
                    ),
                ),
        )

    private fun originalTicket() = ConnectionTicket(
        ticket = "ticket-1",
        renewalToken = "renewal-1",
        launchUrl = "https://console.example/web_client/#instance=i-1",
        expiresAtEpochMillis = 1_000_000L,
        logicalSessionId = "logical-1",
        streamId = "stream-1",
        joinMode = JoinMode.Control,
        permissions = setOf("view", "input"),
        rtcIceConfigJson = "{}",
        relayHost = "relay.example",
        relayPort = 443,
        signalDeviceId = "server_device-1__instance__i-1",
    )
}
