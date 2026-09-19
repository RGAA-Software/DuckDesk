package yun.pixels.client.core.network

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountResult

class ConsoleResourceDescriptorTest {
    @Test
    fun cloudApplicationDescriptorBindsExplicitTargetAndFrontendGrant() {
        val expectedTarget = cloudApplicationTarget("instance-1")
        val result = parseResourceConnection(descriptor("instance-1"), OpenedResourceSession("session-1", 2), expectedTarget, "instance-1")

        require(result is AccountResult.Success)
        assertEquals("render.example.com", result.value.host)
        assertEquals(4613, result.value.port)
        assertEquals("session-1", result.value.sessionId)
        assertEquals(2L, result.value.sessionRevision)
        assertEquals("frontend-secret", result.value.frontendToken)
    }

    @Test
    fun descriptorCannotSubstituteAnotherCloudApplicationInstance() {
        val result = parseResourceConnection(
            descriptor("instance-2"),
            OpenedResourceSession("session-1", 2),
            cloudApplicationTarget("instance-1"),
            "instance-1",
        )

        assertTrue(result is AccountResult.Failure)
    }

    private fun cloudApplicationTarget(instanceId: String) = JSONObject()
        .put("kind", "cloud_application")
        .put("application_id", "application-1")
        .put("instance_id", instanceId)

    private fun descriptor(instanceId: String) = JSONObject(
        """
        {
          "descriptor": {
            "session": {
              "id": "session-1",
              "target": {
                "kind": "cloud_application",
                "application_id": "application-1",
                "instance_id": "$instanceId"
              },
              "owner": {"kind": "user", "user_id": "user-1"},
              "client_type": "android",
              "access_role": "controller",
              "state": "pending",
              "revision": 2,
              "created_at": "2026-09-19T01:00:00Z",
              "closed_at": null
            },
            "node_id": "node-1",
            "node_generation": 1,
            "control_epoch": 1,
            "endpoint_revision": 1,
            "host": "render.example.com",
            "port": 4613,
            "transport": "native",
            "expires_at": "2026-09-19T02:00:00Z"
          },
          "token": "frontend-secret"
        }
        """.trimIndent(),
    )
}
