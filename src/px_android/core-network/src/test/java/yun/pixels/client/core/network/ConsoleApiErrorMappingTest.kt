package yun.pixels.client.core.network

import org.junit.Assert.assertEquals
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountFailure

class ConsoleApiErrorMappingTest {
    @Test
    fun serverErrorNameDistinguishesInvalidLoginFromExpiredSession() {
        assertEquals(AccountFailure.InvalidCredentials, accountFailure(401, """{"error":"AUTH_INVALID_CREDENTIALS"}"""))
        assertEquals(AccountFailure.AuthenticationRequired, accountFailure(401, """{"error":"AUTH_REQUIRED"}"""))
    }

    @Test
    fun deviceOfflineOverridesGenericServiceUnavailableStatus() {
        assertEquals(AccountFailure.DeviceOffline, accountFailure(503, """{"error":"DEVICE_OFFLINE"}"""))
        assertEquals(AccountFailure.ServerError, accountFailure(503, """{"error":"REQUEST_FAILED"}"""))
    }
}
