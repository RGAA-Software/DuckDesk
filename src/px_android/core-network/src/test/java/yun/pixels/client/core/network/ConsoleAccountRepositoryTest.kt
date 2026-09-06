package yun.pixels.client.core.network

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountProfile
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountSessionStore
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.JoinMode

class ConsoleAccountRepositoryTest {
    @Test
    fun restoreClearsExpiredSession() = runTest {
        val store = FakeSessionStore(session(expiresAt = 99))
        val repository = ConsoleAccountRepository(FakeApi(), store, now = { 100 })

        repository.restore()

        assertEquals(AccountState.SignedOut, repository.state.value)
        assertEquals(null, store.session)
    }

    @Test
    fun loginPersistsSessionAndPublishesSignedInState() = runTest {
        val expected = session(expiresAt = 200)
        val store = FakeSessionStore()
        val repository = ConsoleAccountRepository(FakeApi(loginResult = AccountResult.Success(expected)), store, now = { 100 })

        val result = repository.login("https://console.example", "alice", "password")

        assertEquals(AccountResult.Success(expected), result)
        assertEquals(expected, store.session)
        assertEquals(AccountState.SignedIn(expected), repository.state.value)
    }

    @Test
    fun authenticationFailureClearsStoredSession() = runTest {
        val store = FakeSessionStore(session(expiresAt = 200))
        val api = FakeApi(devicesResult = AccountResult.Failure(AccountFailure.AuthenticationRequired))
        val repository = ConsoleAccountRepository(api, store, now = { 100 })
        repository.restore()

        val result = repository.devices()

        assertEquals(AccountResult.Failure(AccountFailure.AuthenticationRequired), result)
        assertEquals(AccountState.SignedOut, repository.state.value)
        assertEquals(null, store.session)
    }

    @Test
    fun endpointRequiresHttpsAndNoEmbeddedPath() {
        assertEquals(ConsoleEndpoint("https://console.example:443"), normalizeEndpoint("https://console.example:443/"))
        assertEquals(null, normalizeEndpoint("http://console.example"))
        assertEquals(null, normalizeEndpoint("https://console.example/api"))
        assertEquals(null, normalizeEndpoint("https://user@console.example"))
        assertTrue(normalizeEndpoint("https://[2001:db8::1]:8443") != null)
    }

    private fun session(expiresAt: Long) = AccountSession(
        endpoint = ConsoleEndpoint("https://console.example"),
        profile = AccountProfile("u1", "alice", null, false),
        accessToken = "token",
        expiresAtEpochMillis = expiresAt,
        absoluteExpiresAtEpochMillis = expiresAt + 100,
    )
}

private class FakeSessionStore(var session: AccountSession? = null) : AccountSessionStore {
    override suspend fun load() = session

    override suspend fun save(session: AccountSession) {
        this.session = session
    }

    override suspend fun clear() {
        session = null
    }
}

private class FakeApi(
    private val loginResult: AccountResult<AccountSession> = AccountResult.Failure(AccountFailure.InvalidCredentials),
    private val devicesResult: AccountResult<List<AccountDevice>> = AccountResult.Success(emptyList()),
) : ConsoleAccountApi {
    override suspend fun login(endpointInput: String, username: String, password: String) = loginResult

    override suspend fun logout(session: AccountSession): AccountResult<Unit> = AccountResult.Success(Unit)

    override suspend fun devices(session: AccountSession) = devicesResult

    override suspend fun issueTicket(
        session: AccountSession,
        deviceId: String,
        clientNonce: String,
        joinMode: JoinMode,
    ): AccountResult<ConnectionTicket> = AccountResult.Failure(AccountFailure.DeviceOffline)
}
