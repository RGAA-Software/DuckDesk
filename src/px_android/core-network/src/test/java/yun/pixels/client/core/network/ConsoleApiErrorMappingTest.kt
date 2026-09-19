package yun.pixels.client.core.network

import org.junit.Assert.assertEquals
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountFailure

class ConsoleApiErrorMappingTest {
    @Test
    fun currentAuthenticationAndAuthorizationCodesRemainTyped() {
        assertEquals(AccountFailure.AuthenticationRequired, accountFailure(HttpResponse(401, """{"code":"unauthorized"}""")))
        assertEquals(AccountFailure.Forbidden, accountFailure(HttpResponse(403, """{"code":"rejected"}""")))
    }

    @Test
    fun serviceFailureAndMissingResourceRemainTyped() {
        assertEquals(AccountFailure.ServerError, accountFailure(HttpResponse(503, """{"code":"unavailable"}""")))
        assertEquals(AccountFailure.NotFound, accountFailure(HttpResponse(404, """{"code":"not_found"}""")))
    }

    @Test
    fun currentConflictAndRateLimitCodesRemainTyped() {
        assertEquals(AccountFailure.InstanceBusy, accountFailure(HttpResponse(409, """{"code":"conflict"}""")))
        assertEquals(AccountFailure.RateLimited, accountFailure(HttpResponse(429, """{"code":"rate_limited"}""")))
    }
}
