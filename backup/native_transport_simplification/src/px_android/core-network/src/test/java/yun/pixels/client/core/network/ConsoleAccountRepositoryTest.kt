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

    @Test
    fun renewalUsesRotatingCapabilityWithoutChangingAccountState() = runTest {
        val current = ticket("ticket-1", "renewal-1")
        val renewed = ticket("ticket-2", "renewal-2")
        val signedIn = session(expiresAt = 200)
        val api = FakeApi(
            loginResult = AccountResult.Success(signedIn),
            renewalResult = AccountResult.Success(renewed),
        )
        val repository = ConsoleAccountRepository(api, FakeSessionStore(), now = { 100 })
        repository.login("https://console.example", "alice", "password")

        assertEquals(AccountResult.Success(renewed), repository.renewTicket(current, "nonce"))
        assertEquals(AccountState.SignedIn(signedIn), repository.state.value)
    }

    private fun session(expiresAt: Long) = AccountSession(
        endpoint = ConsoleEndpoint("https://console.example"),
        profile = AccountProfile("u1", "alice", null, false),
        accessToken = "token",
        expiresAtEpochMillis = expiresAt,
        absoluteExpiresAtEpochMillis = expiresAt + 100,
    )

    private fun ticket(raw: String, renewal: String) = ConnectionTicket(
        ticket = raw,
        renewalToken = renewal,
        launchUrl = "https://console.example/web_client/",
        expiresAtEpochMillis = 1_000,
        logicalSessionId = "logical",
        streamId = "stream",
        joinMode = JoinMode.Control,
        permissions = setOf("view"),
        rtcIceConfigJson = "{}",
        relayHost = "relay.example",
        relayPort = 443,
        signalDeviceId = "server_device",
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
    private val renewalResult: AccountResult<ConnectionTicket> = AccountResult.Failure(AccountFailure.AuthenticationRequired),
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

    override suspend fun renewTicket(
        session: AccountSession,
        ticket: ConnectionTicket,
        clientNonce: String,
    ): AccountResult<ConnectionTicket> = renewalResult
}
