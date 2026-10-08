package yun.pixels.client.core.network

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountProfile
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountSessionStore
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.ConsoleEndpointStore
import yun.pixels.client.core.domain.account.GuestSession
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import yun.pixels.client.core.domain.account.ResourceConnection
import yun.pixels.client.core.domain.account.ResourceConnectionOwner
import yun.pixels.client.core.domain.session.RemoteResourceConnectionRenewal
import yun.pixels.client.core.domain.session.RemoteSessionFailure
import yun.pixels.client.core.domain.session.RemoteSessionTarget

class ConsoleResourceConnectionRenewerTest {
    @Test
    fun signedInDesktopRenewsTheExistingUserSession() = runTest {
        val endpoint = ConsoleEndpoint("https://console.example")
        val accountSession = accountSession(endpoint)
        val renewedConnection = connection(ResourceConnectionOwner.User, revision = 4)
        val api = RenewalApi(
            loginResult = AccountResult.Success(accountSession),
            userRenewalResult = AccountResult.Success(renewedConnection),
        )
        val sessions = coordinator(api)
        sessions.login(endpoint.baseUrl, "alice", "password")
        val renewer = ConsoleResourceConnectionRenewer(api, api, sessions)

        val result = renewer.renew(
            RemoteSessionTarget.Account("Office", "device-1", connection(ResourceConnectionOwner.User, revision = 3)),
        )

        assertEquals(RemoteResourceConnectionRenewal.Renewed(renewedConnection), result)
        assertEquals(1, api.userDesktopRenewals)
    }

    @Test
    fun guestCloudApplicationRenewsOnlyWithTheCachedGuestIdentity() = runTest {
        val renewedConnection = connection(ResourceConnectionOwner.Guest, revision = 4)
        val api = RenewalApi(guestRenewalResult = AccountResult.Success(renewedConnection))
        val sessions = coordinator(api)
        sessions.restore()
        sessions.guestSession()
        val renewer = ConsoleResourceConnectionRenewer(api, api, sessions)

        val result = renewer.renew(
            RemoteSessionTarget.CloudApplication(
                "Cloud Game",
                "application-1",
                "instance-1",
                connection(ResourceConnectionOwner.Guest, revision = 3),
            ),
        )

        assertEquals(RemoteResourceConnectionRenewal.Renewed(renewedConnection), result)
        assertEquals(1, api.guestSessionRequests)
        assertEquals(1, api.guestCloudRenewals)
    }

    @Test
    fun expiredGuestIdentityDoesNotCreateAReplacementOwner() = runTest {
        var currentTime = 100L
        val api = RenewalApi(guestExpiresAt = 150L)
        val sessions = coordinator(api, now = { currentTime })
        sessions.restore()
        sessions.guestSession()
        currentTime = 200L
        val renewer = ConsoleResourceConnectionRenewer(api, api, sessions)

        val result = renewer.renew(
            RemoteSessionTarget.CloudApplication(
                "Cloud Game",
                "application-1",
                "instance-1",
                connection(ResourceConnectionOwner.Guest, revision = 3),
            ),
        )

        assertEquals(RemoteResourceConnectionRenewal.Rejected(RemoteSessionFailure.AuthenticationRejected), result)
        assertEquals(1, api.guestSessionRequests)
        assertEquals(0, api.guestCloudRenewals)
    }

    private fun coordinator(api: RenewalApi, now: () -> Long = { 100L }) = ConsoleSessionCoordinator(
        api,
        RenewalEndpointStore(ConsoleEndpoint("https://console.example")),
        RenewalSessionStore(),
        now,
    )

    private fun accountSession(endpoint: ConsoleEndpoint) = AccountSession(
        endpoint,
        AccountProfile("user-1", "alice", null, false),
        "user-token",
        1_000L,
    )

    private fun connection(owner: ResourceConnectionOwner, revision: Long) = ResourceConnection(
        host = "render.example",
        port = 4613,
        remoteResourceId = if (owner == ResourceConnectionOwner.User) "device-1" else "instance-1",
        sessionId = "resource-session-1",
        sessionRevision = revision,
        frontendToken = "frontend-token-$revision",
        transport = "native",
        expiresAtEpochMillis = 1_000L,
        owner = owner,
    )
}

private class RenewalApi(
    private val loginResult: AccountResult<AccountSession> = AccountResult.Failure(AccountFailure.InvalidCredentials),
    private val userRenewalResult: AccountResult<ResourceConnection> = AccountResult.Failure(AccountFailure.NetworkUnavailable),
    private val guestRenewalResult: AccountResult<ResourceConnection> = AccountResult.Failure(AccountFailure.NetworkUnavailable),
    private val guestExpiresAt: Long = 1_000L,
) : ConsoleAccountApi, ConsoleApplicationApi {
    var guestSessionRequests = 0
    var userDesktopRenewals = 0
    var guestCloudRenewals = 0

    override suspend fun testEndpoint(endpointInput: String) = AccountResult.Success(ConsoleEndpoint(endpointInput))

    override suspend fun guestSession(endpoint: ConsoleEndpoint): AccountResult<GuestSession> {
        guestSessionRequests += 1
        return AccountResult.Success(GuestSession(endpoint, "guest-token", guestExpiresAt))
    }

    override suspend fun register(endpoint: ConsoleEndpoint, username: String, password: String) =
        AccountResult.Success(AccountProfile("user-1", username, null, false))

    override suspend fun login(endpointInput: String, username: String, password: String) = loginResult

    override suspend fun logout(session: AccountSession): AccountResult<Unit> = AccountResult.Success(Unit)

    override suspend fun devices(session: AccountSession): AccountResult<List<AccountDevice>> = AccountResult.Success(emptyList())

    override suspend fun resolveConnection(session: AccountSession, deviceId: String) = userRenewalResult

    override suspend fun renewConnection(session: AccountSession, deviceId: String, connection: ResourceConnection) =
        userRenewalResult.also { userDesktopRenewals += 1 }

    override suspend fun applications(session: AccountSession): AccountResult<List<RemoteApplication>> = AccountResult.Success(emptyList())

    override suspend fun startApplication(session: AccountSession, appId: String, clientNonce: String) = unsupportedInstance()

    override suspend fun stopApplication(session: AccountSession, instanceId: String): AccountResult<Unit> = unsupported()

    override suspend fun resolveApplicationConnection(session: AccountSession, appId: String, instanceId: String) = userRenewalResult

    override suspend fun renewApplicationConnection(
        session: AccountSession,
        appId: String,
        instanceId: String,
        connection: ResourceConnection,
    ) = userRenewalResult

    override suspend fun publicApplications(session: GuestSession): AccountResult<List<RemoteApplication>> = AccountResult.Success(emptyList())

    override suspend fun startGuestApplication(session: GuestSession, appId: String, clientNonce: String) = unsupportedInstance()

    override suspend fun stopGuestApplication(session: GuestSession, instanceId: String): AccountResult<Unit> = unsupported()

    override suspend fun resolveGuestApplicationConnection(session: GuestSession, appId: String, instanceId: String) = guestRenewalResult

    override suspend fun renewGuestApplicationConnection(
        session: GuestSession,
        appId: String,
        instanceId: String,
        connection: ResourceConnection,
    ) = guestRenewalResult.also { guestCloudRenewals += 1 }

    private fun unsupportedInstance(): AccountResult<RemoteApplicationInstance> = AccountResult.Failure(AccountFailure.ServerError)

    private fun <T> unsupported(): AccountResult<T> = AccountResult.Failure(AccountFailure.ServerError)
}

private class RenewalEndpointStore(private var endpoint: ConsoleEndpoint?) : ConsoleEndpointStore {
    override suspend fun load() = endpoint

    override suspend fun save(endpoint: ConsoleEndpoint) {
        this.endpoint = endpoint
    }

    override suspend fun clear() {
        endpoint = null
    }
}

private class RenewalSessionStore : AccountSessionStore {
    private var session: AccountSession? = null

    override suspend fun load() = session

    override suspend fun save(session: AccountSession) {
        this.session = session
    }

    override suspend fun clear() {
        session = null
    }
}
