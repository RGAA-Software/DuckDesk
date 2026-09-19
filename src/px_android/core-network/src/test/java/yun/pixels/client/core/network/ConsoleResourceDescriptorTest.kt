package yun.pixels.client.core.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.runTest
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountProfile
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.ResourceConnection
import yun.pixels.client.core.domain.account.ResourceConnectionOwner

class ConsoleResourceDescriptorTest {
    @Test
    fun renewalReissuesOnlyTheExistingResourceSession() = runTest {
        val requests = mutableListOf<RecordedRequest>()
        val requestExecutor = ConsoleRequestExecutor { _, path, method, bearerToken, subjectKind, body ->
            requests += RecordedRequest(path, method, bearerToken, subjectKind, body?.toString())
            HttpResponse(200, desktopDescriptor())
        }
        val client = ConsoleApiClient(Dispatchers.Unconfined, requestExecutor)
        val session = AccountSession(
            ConsoleEndpoint("https://console.example"),
            AccountProfile("user-1", "alice", null, false),
            "account-token",
            Long.MAX_VALUE,
        )
        val currentConnection = ResourceConnection(
            "render.example.com",
            4601,
            "device-1",
            "session-1",
            3,
            "old-frontend-token",
            "native",
            Long.MAX_VALUE,
            owner = ResourceConnectionOwner.User,
        )

        val result = client.renewConnection(session, "device-1", currentConnection)

        require(result is AccountResult.Success)
        assertEquals(4L, result.value.sessionRevision)
        assertEquals("new-frontend-token", result.value.frontendToken)
        assertEquals(
            listOf(
                RecordedRequest(
                    "/api/console/resource-sessions/session-1/descriptor",
                    "POST",
                    "account-token",
                    "user",
                    "{\"revision\":3}",
                ),
            ),
            requests,
        )
    }

    @Test
    fun renewalRequestUsesTheExistingSessionAndRevision() {
        val request = resourceDescriptorRequest("session/one", 7)

        assertEquals("/api/console/resource-sessions/session%2Fone/descriptor", request.path)
        assertEquals(7L, request.body.getLong("revision"))
        assertEquals(1, request.body.length())
    }

    @Test
    fun cloudApplicationDescriptorBindsExplicitTargetAndFrontendGrant() {
        val expectedTarget = cloudApplicationTarget("instance-1")
        val result = parseResourceConnection(
            descriptor("instance-1"),
            OpenedResourceSession("session-1", 2),
            expectedTarget,
            "instance-1",
            ResourceConnectionOwner.User,
        )

        require(result is AccountResult.Success)
        assertEquals("render.example.com", result.value.host)
        assertEquals(4613, result.value.port)
        assertEquals("session-1", result.value.sessionId)
        assertEquals(2L, result.value.sessionRevision)
        assertEquals("frontend-secret", result.value.frontendToken)
        assertEquals("relay.example.com", result.value.relay?.host)
        assertEquals(4605, result.value.relay?.port)
        assertEquals(RELAY_ADMISSION_TICKET, result.value.relay?.admissionTicket)
    }

    @Test
    fun descriptorCannotSubstituteAnotherCloudApplicationInstance() {
        val result = parseResourceConnection(
            descriptor("instance-2"),
            OpenedResourceSession("session-1", 2),
            cloudApplicationTarget("instance-1"),
            "instance-1",
            ResourceConnectionOwner.User,
        )

        assertTrue(result is AccountResult.Failure)
    }

    @Test
    fun descriptorCannotChangeTheAuthenticatedOwnerKind() {
        val result = parseResourceConnection(
            descriptor("instance-1", ownerKind = "guest"),
            OpenedResourceSession("session-1", 2),
            cloudApplicationTarget("instance-1"),
            "instance-1",
            ResourceConnectionOwner.User,
        )

        assertTrue(result is AccountResult.Failure)
    }

    private fun cloudApplicationTarget(instanceId: String) = JSONObject()
        .put("kind", "cloud_application")
        .put("application_id", "application-1")
        .put("instance_id", instanceId)

    private fun descriptor(instanceId: String, ownerKind: String = "user") = JSONObject(
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
              "owner": {"kind": "$ownerKind", "user_id": "user-1"},
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
          "token": "frontend-secret",
          "relay": {
            "host": "relay.example.com",
            "port": 4605,
            "admission_ticket": "$RELAY_ADMISSION_TICKET"
          }
        }
        """.trimIndent(),
    )

    private fun desktopDescriptor() =
        """
        {
          "descriptor": {
            "session": {
              "id": "session-1",
              "target": {"kind": "desktop", "device_id": "device-1"},
              "owner": {"kind": "user", "user_id": "user-1"},
              "client_type": "android",
              "access_role": "controller",
              "state": "connected",
              "revision": 4
            },
            "host": "render.example.com",
            "port": 4601,
            "transport": "native",
            "expires_at": "2026-09-20T12:00:00Z"
          },
          "token": "new-frontend-token"
        }
        """.trimIndent()

    private companion object {
        const val RELAY_ADMISSION_TICKET =
            "pxr1.1789783200.10000000-0000-0000-0000-000000000001.20000000-0000-0000-0000-000000000002." +
                "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
}

private data class RecordedRequest(
    val path: String,
    val method: String,
    val bearerToken: String?,
    val subjectKind: String?,
    val body: String?,
)
