package yun.pixels.client.core.nativebridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import yun.pixels.client.core.domain.account.ResourceConnection
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionTarget

class ConsoleResourceSessionConfigTest {
    @Test
    fun cloudApplicationUsesExplicitFrontendGrantWithoutPasswordFallback() {
        val connection = connection(remoteResourceId = "instance-1")
        val request = RemoteSessionRequest(
            id = RemoteSessionId("local-session"),
            target = RemoteSessionTarget.CloudApplication("Cloud Game", "application-1", "instance-1", connection),
        )

        val config = request.toNativeConfig("android-device", null)

        requireNotNull(config)
        assertEquals("session-1", config.streamId)
        assertEquals("session-1", config.frontendSessionId)
        assertEquals(4L, config.frontendSessionRevision)
        assertEquals("frontend-secret", config.frontendToken)
        assertEquals("", config.remotePasswordHash)
        assertEquals("instance-1", config.remoteDeviceId)
        assertEquals("instance-1", config.connectionInstanceId)
    }

    @Test
    fun accountDesktopUsesDescriptorSessionAndRejectsIncompleteGrant() {
        val complete = RemoteSessionRequest(
            id = RemoteSessionId("local-session"),
            target = RemoteSessionTarget.Account("Office", "device-1", connection(remoteResourceId = "device-1")),
        )
        val incomplete = complete.copy(
            target = RemoteSessionTarget.Account(
                "Office",
                "device-1",
                connection(remoteResourceId = "device-1").copy(frontendToken = ""),
            ),
        )

        val config = complete.toNativeConfig("android-device", null)

        requireNotNull(config)
        assertEquals("session-1", config.connectionNonce)
        assertEquals("", config.connectionInstanceId)
        assertNull(incomplete.toNativeConfig("android-device", null))
    }

    private fun connection(remoteResourceId: String) = ResourceConnection(
        host = "render.example.com",
        port = 4613,
        remoteResourceId = remoteResourceId,
        sessionId = "session-1",
        sessionRevision = 4,
        frontendToken = "frontend-secret",
        transport = "native",
        expiresAtEpochMillis = Long.MAX_VALUE,
    )
}
