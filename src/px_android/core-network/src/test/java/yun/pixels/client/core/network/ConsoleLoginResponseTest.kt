package yun.pixels.client.core.network

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.ConsoleEndpoint

class ConsoleLoginResponseTest {
    @Test
    fun currentLoginResponseDoesNotRequireRetiredAbsoluteExpiry() {
        val result = parseLogin(
            ConsoleEndpoint("https://console.example"),
            JSONObject(
                """{
                    "token":"${"a".repeat(64)}",
                    "expires_at":"2026-09-20T00:00:00Z",
                    "client_type":"android",
                    "profile":{
                        "id":"00000000-0000-4000-8000-000000000001",
                        "username":"alice",
                        "avatar_url":null,
                        "role":"user",
                        "revision":1,
                        "authorization_revision":1,
                        "created_at":"2026-09-19T00:00:00Z"
                    }
                }""",
            ),
        )

        assertTrue(result is AccountResult.Success)
        val session = (result as AccountResult.Success).value
        assertEquals("alice", session.profile.username)
        assertNull(session.profile.avatarPath)
        assertEquals(1_789_862_400_000L, session.expiresAtEpochMillis)
    }
}
