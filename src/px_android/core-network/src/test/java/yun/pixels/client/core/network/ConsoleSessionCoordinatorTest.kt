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
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.ConsoleEndpointStore
import yun.pixels.client.core.domain.account.GuestSession
import yun.pixels.client.core.domain.account.ResourceConnection

class ConsoleSessionCoordinatorTest {
    @Test
    fun restoreClearsExpiredSession() = runTest {
        val store = FakeSessionStore(session(expiresAt = 99))
        val repository = ConsoleSessionCoordinator(FakeApi(), FakeEndpointStore(), store, now = { 100 })

        repository.restore()

        assertEquals(AccountState.SignedOut, repository.state.value)
        assertEquals(null, store.session)
    }

    @Test
    fun loginPersistsSessionAndPublishesSignedInState() = runTest {
        val expected = session(expiresAt = 200)
        val store = FakeSessionStore()
        val repository = ConsoleSessionCoordinator(FakeApi(loginResult = AccountResult.Success(expected)), FakeEndpointStore(), store, now = { 100 })

        val result = repository.login("https://console.example", "alice", "password")

        assertEquals(AccountResult.Success(expected), result)
        assertEquals(expected, store.session)
        assertEquals(AccountState.SignedIn(expected), repository.state.value)
    }

    @Test
    fun authenticationFailureClearsStoredSession() = runTest {
        val store = FakeSessionStore(session(expiresAt = 200))
        val api = FakeApi(devicesResult = AccountResult.Failure(AccountFailure.AuthenticationRequired))
        val repository = ConsoleSessionCoordinator(api, FakeEndpointStore(), store, now = { 100 })
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

    @Test
    fun resolvingConnectionKeepsAccountState() = runTest {
        val connection = connection()
        val signedIn = session(expiresAt = 200)
        val api = FakeApi(
            loginResult = AccountResult.Success(signedIn),
            connectionResult = AccountResult.Success(connection),
        )
        val repository = ConsoleSessionCoordinator(api, FakeEndpointStore(), FakeSessionStore(), now = { 100 })
        repository.login("https://console.example", "alice", "password")

        assertEquals(AccountResult.Success(connection), repository.resolveConnection("device"))
        assertEquals(AccountState.SignedIn(signedIn), repository.state.value)
    }

    private fun session(expiresAt: Long) = AccountSession(
        endpoint = ConsoleEndpoint("https://console.example"),
        profile = AccountProfile("u1", "alice", null, false),
        accessToken = "token",
        expiresAtEpochMillis = expiresAt,
    )

    private fun connection() = ResourceConnection(
        host = "render.example",
        port = 4601,
        remoteResourceId = "device",
        sessionId = "session",
        sessionRevision = 1,
        frontendToken = "frontend-token",
        transport = "native",
        expiresAtEpochMillis = Long.MAX_VALUE,
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
    private val connectionResult: AccountResult<ResourceConnection> = AccountResult.Failure(AccountFailure.AuthenticationRequired),
) : ConsoleAccountApi {
    override suspend fun testEndpoint(endpointInput: String) = AccountResult.Success(ConsoleEndpoint(endpointInput))

    override suspend fun guestSession(endpoint: ConsoleEndpoint) =
        AccountResult.Success(GuestSession(endpoint, "guest", Long.MAX_VALUE))

    override suspend fun register(endpoint: ConsoleEndpoint, username: String, password: String) =
        AccountResult.Success(AccountProfile("new", username, null, false))

    override suspend fun login(endpointInput: String, username: String, password: String) = loginResult

    override suspend fun logout(session: AccountSession): AccountResult<Unit> = AccountResult.Success(Unit)

    override suspend fun devices(session: AccountSession) = devicesResult

    override suspend fun resolveConnection(session: AccountSession, deviceId: String): AccountResult<ResourceConnection> = connectionResult
}

private class FakeEndpointStore(private var endpoint: ConsoleEndpoint? = ConsoleEndpoint("https://console.example")) : ConsoleEndpointStore {
    override suspend fun load() = endpoint
    override suspend fun save(endpoint: ConsoleEndpoint) { this.endpoint = endpoint }
    override suspend fun clear() { endpoint = null }
}
