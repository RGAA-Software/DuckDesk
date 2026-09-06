package yun.pixels.client.core.nativebridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class StandardRtcSignalingTest {
    @Test
    fun requestUrlCarriesEveryTicketScopeBinding() {
        val url = buildRequestUrl(parameters(instanceId = "instance-9"), "web_aabbcc")

        assertEquals("https", url.scheme)
        assertEquals("relay.example.com", url.host)
        assertEquals(443, url.port)
        assertEquals("/relay", url.encodedPath)
        assertEquals("web_aabbcc", url.queryParameter("device_id"))
        assertEquals("server_device-7", url.queryParameter("remote_device_id"))
        assertEquals("device-7", url.queryParameter("ticket_device_id"))
        assertEquals("stream-3", url.queryParameter("stream_id"))
        assertEquals("1", url.queryParameter("rtc_signal"))
        assertEquals("ticket-value", url.queryParameter("ticket"))
        assertEquals("nonce-value", url.queryParameter("client_nonce"))
        assertEquals("instance-9", url.queryParameter("instance_id"))
        assertTrue(url.queryParameter("device_name").orEmpty().isNotBlank())
    }

    @Test
    fun requestUrlOmitsEmptyOptionalInstance() {
        val url = buildRequestUrl(parameters(instanceId = ""), "web_aabbcc")

        assertNull(url.queryParameter("instance_id"))
    }

    private fun parameters(instanceId: String) = StandardRtcSignalParameters(
        relayHost = "relay.example.com",
        relayPort = 443,
        secure = true,
        remoteDeviceId = "server_device-7",
        ticketDeviceId = "device-7",
        streamId = "stream-3",
        ticket = "ticket-value",
        clientNonce = "nonce-value",
        instanceId = instanceId,
    )
}
