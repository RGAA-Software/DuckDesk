package yun.pixels.client.core.network

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.test.runCurrent
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
    fun unifiedPixelsPackageSwitchesBetweenOfficialAndCustomEndpoints() = runTest {
        val officialEndpoint = ConsoleEndpoint("https://official-console.example")
        val repository = ConsoleSessionCoordinator(
            FakeApi(),
            FakeEndpointStore(null),
            FakeSessionStore(),
            officialEndpoint = officialEndpoint.baseUrl,
        )

        repository.restore()
        assertEquals(officialEndpoint, repository.endpoint.value)
        assertTrue(repository.endpointEditable)
        assertEquals(
            AccountResult.Failure(AccountFailure.InvalidEndpoint),
            repository.saveEndpoint(officialEndpoint.baseUrl),
        )
        assertEquals(
            AccountResult.Success(ConsoleEndpoint("https://private-console.example")),
            repository.saveEndpoint("https://private-console.example"),
        )
        assertEquals(AccountResult.Success(officialEndpoint), repository.selectOfficialEndpoint())
        assertEquals(officialEndpoint, repository.endpoint.value)
    }

    @Test
    fun switchingConsoleClearsThePreviousAccount() = runTest {
        val officialEndpoint = ConsoleEndpoint("https://official-console.example")
        val privateEndpoint = ConsoleEndpoint("https://private-console.example")
        val privateSession = session(expiresAt = 200, endpoint = privateEndpoint)
        val sessionStore = FakeSessionStore(privateSession)
        val repository = ConsoleSessionCoordinator(
            FakeApi(),
            FakeEndpointStore(privateEndpoint),
            sessionStore,
            now = { 100 },
            officialEndpoint = officialEndpoint.baseUrl,
        )

        repository.restore()
        assertEquals(AccountState.SignedIn(privateSession), repository.state.value)
        assertEquals(AccountResult.Success(officialEndpoint), repository.selectOfficialEndpoint())
        assertEquals(AccountState.SignedOut, repository.state.value)
        assertEquals(null, sessionStore.session)
    }

    @Test
    fun completedLoginCannotRestoreAnAccountAfterConsoleSwitch() = runTest {
        val officialEndpoint = ConsoleEndpoint("https://official-console.example")
        val privateEndpoint = ConsoleEndpoint("https://private-console.example")
        val pendingLogin = CompletableDeferred<AccountResult<AccountSession>>()
        val sessionStore = FakeSessionStore()
        val repository = ConsoleSessionCoordinator(
            FakeApi(loginOperation = { pendingLogin.await() }),
            FakeEndpointStore(privateEndpoint),
            sessionStore,
            officialEndpoint = officialEndpoint.baseUrl,
        )
        repository.restore()

        val loginResult = async { repository.login(privateEndpoint.baseUrl, "alice", "password") }
        runCurrent()
        assertEquals(AccountResult.Success(officialEndpoint), repository.selectOfficialEndpoint())
        pendingLogin.complete(AccountResult.Success(session(expiresAt = 200, endpoint = privateEndpoint)))

        assertEquals(AccountResult.Failure(AccountFailure.InvalidEndpoint), loginResult.await())
        assertEquals(AccountState.SignedOut, repository.state.value)
        assertEquals(null, sessionStore.session)
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

    private fun session(expiresAt: Long, endpoint: ConsoleEndpoint = ConsoleEndpoint("https://console.example")) = AccountSession(
        endpoint = endpoint,
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
    private val loginOperation: (suspend () -> AccountResult<AccountSession>)? = null,
    private val devicesResult: AccountResult<List<AccountDevice>> = AccountResult.Success(emptyList()),
    private val connectionResult: AccountResult<ResourceConnection> = AccountResult.Failure(AccountFailure.AuthenticationRequired),
) : ConsoleAccountApi {
    override suspend fun testEndpoint(endpointInput: String) = AccountResult.Success(ConsoleEndpoint(endpointInput))

    override suspend fun guestSession(endpoint: ConsoleEndpoint) =
        AccountResult.Success(GuestSession(endpoint, "guest", Long.MAX_VALUE))

    override suspend fun register(endpoint: ConsoleEndpoint, username: String, password: String) =
        AccountResult.Success(AccountProfile("new", username, null, false))

    override suspend fun login(endpointInput: String, username: String, password: String) = loginOperation?.invoke() ?: loginResult

    override suspend fun logout(session: AccountSession): AccountResult<Unit> = AccountResult.Success(Unit)

    override suspend fun devices(session: AccountSession) = devicesResult

    override suspend fun resolveConnection(session: AccountSession, deviceId: String): AccountResult<ResourceConnection> = connectionResult

    override suspend fun renewConnection(
        session: AccountSession,
        deviceId: String,
        connection: ResourceConnection,
    ): AccountResult<ResourceConnection> = connectionResult
}

private class FakeEndpointStore(private var endpoint: ConsoleEndpoint? = ConsoleEndpoint("https://console.example")) : ConsoleEndpointStore {
    override suspend fun load() = endpoint
    override suspend fun save(endpoint: ConsoleEndpoint) { this.endpoint = endpoint }
    override suspend fun clear() { endpoint = null }
}
