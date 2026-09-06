package yun.pixels.client.core.nativebridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DirectSessionAuthorizerTest {
    @Test
    fun acceptsFreshPreparedStreamReceipt() {
        val result = parseDirectSessionAuthorization(
            """{"code":200,"data":{"stream_id":"ip-direct:0123456789abcdef","expires_at_ms":2000}}""",
            "nonce",
            1000,
        )

        assertEquals(
            DirectSessionAuthorizationResult.Authorized(DirectSessionAuthorization("ip-direct:0123456789abcdef", "nonce")),
            result,
        )
    }

    @Test
    fun rejectsExpiredOrUnpreparedReceipts() {
        assertTrue(
            parseDirectSessionAuthorization(
                """{"code":200,"data":{"stream_id":"caller-value","expires_at_ms":2000}}""",
                "nonce",
                1000,
            ) is DirectSessionAuthorizationResult.Rejected,
        )
        assertTrue(
            parseDirectSessionAuthorization(
                """{"code":200,"data":{"stream_id":"ip-direct:0123","expires_at_ms":1000}}""",
                "nonce",
                1000,
            ) is DirectSessionAuthorizationResult.Rejected,
        )
    }
}
