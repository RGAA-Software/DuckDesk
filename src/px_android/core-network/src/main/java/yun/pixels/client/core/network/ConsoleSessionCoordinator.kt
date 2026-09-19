package yun.pixels.client.core.network

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountSessionStore
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.ConsoleEndpointStore
import yun.pixels.client.core.domain.account.ConsoleSessionRepository
import yun.pixels.client.core.domain.account.GuestSession
import yun.pixels.client.core.domain.account.ResourceConnection

class ConsoleSessionCoordinator(
    private val api: ConsoleAccountApi,
    private val endpointStore: ConsoleEndpointStore,
    private val sessionStore: AccountSessionStore,
    private val now: () -> Long = System::currentTimeMillis,
) : ConsoleSessionRepository {
    private val mutableState = MutableStateFlow<AccountState>(AccountState.Loading)
    private val mutableEndpoint = MutableStateFlow<ConsoleEndpoint?>(null)
    private val guestMutex = Mutex()
    private var guestSession: GuestSession? = null

    override val state: StateFlow<AccountState> = mutableState.asStateFlow()
    override val endpoint: StateFlow<ConsoleEndpoint?> = mutableEndpoint.asStateFlow()

    override suspend fun restore() {
        val endpoint = endpointStore.load()
        mutableEndpoint.value = endpoint
        val session = sessionStore.load()?.takeIf {
            endpoint != null && it.endpoint == endpoint && it.expiresAtEpochMillis > now()
        }
        if (session == null) sessionStore.clear()
        mutableState.value = if (session == null) AccountState.SignedOut else AccountState.SignedIn(session)
    }

    override suspend fun saveEndpoint(endpoint: String): AccountResult<ConsoleEndpoint> {
        val normalized = normalizeEndpoint(endpoint)
            ?: return AccountResult.Failure(AccountFailure.InvalidEndpoint)
        if (mutableEndpoint.value != normalized) {
            sessionStore.clear()
            guestMutex.withLock { guestSession = null }
            mutableState.value = AccountState.SignedOut
            mutableEndpoint.value = normalized
            endpointStore.save(normalized)
        }
        return AccountResult.Success(normalized)
    }

    override suspend fun testEndpoint(endpoint: String): AccountResult<ConsoleEndpoint> = api.testEndpoint(endpoint)

    override suspend fun login(endpoint: String, username: String, password: String): AccountResult<AccountSession> {
        val saved = saveEndpoint(endpoint)
        if (saved is AccountResult.Failure) return saved
        val normalized = (saved as AccountResult.Success).value
        mutableState.value = AccountState.Loading
        return when (val result = api.login(normalized.baseUrl, username, password)) {
            is AccountResult.Success -> {
                sessionStore.save(result.value)
                guestMutex.withLock { guestSession = null }
                mutableState.value = AccountState.SignedIn(result.value)
                result
            }
            is AccountResult.Failure -> {
                mutableState.value = AccountState.SignedOut
                result
            }
        }
    }

    override suspend fun register(username: String, password: String): AccountResult<AccountSession> {
        if (!validUsername(username) || password.length !in 8..128 || password.isBlank()) {
            return AccountResult.Failure(AccountFailure.InvalidCredentials)
        }
        val endpoint = mutableEndpoint.value ?: return AccountResult.Failure(AccountFailure.InvalidEndpoint)
        return when (val registered = api.register(endpoint, username, password)) {
            is AccountResult.Failure -> registered
            is AccountResult.Success -> when (val login = login(endpoint.baseUrl, username, password)) {
                is AccountResult.Success -> login
                is AccountResult.Failure -> AccountResult.Failure(AccountFailure.AccountCreatedLoginFailed)
            }
        }
    }

    override suspend fun logout(): AccountResult<Unit> {
        val session = currentUserSession() ?: return AccountResult.Success(Unit)
        val result = api.logout(session)
        sessionStore.clear()
        guestMutex.withLock { guestSession = null }
        mutableState.value = AccountState.SignedOut
        return result
    }

    override suspend fun devices(): AccountResult<List<AccountDevice>> = withUserSession(api::devices)

    override suspend fun resolveConnection(deviceId: String): AccountResult<ResourceConnection> =
        withUserSession { session -> api.resolveConnection(session, deviceId) }

    internal fun currentUserSession(): AccountSession? = (mutableState.value as? AccountState.SignedIn)?.session
        ?.takeIf { it.expiresAtEpochMillis > now() }

    internal suspend fun guestSession(): AccountResult<GuestSession> = guestMutex.withLock {
        guestSession?.takeIf { it.expiresAtEpochMillis > now() && it.endpoint == mutableEndpoint.value }
            ?.let { return AccountResult.Success(it) }
        val endpoint = mutableEndpoint.value ?: return AccountResult.Failure(AccountFailure.InvalidEndpoint)
        when (val result = api.guestSession(endpoint)) {
            is AccountResult.Success -> {
                guestSession = result.value
                result
            }
            is AccountResult.Failure -> result
        }
    }

    internal suspend fun invalidateGuest() {
        guestMutex.withLock { guestSession = null }
    }

    private suspend fun <T> withUserSession(block: suspend (AccountSession) -> AccountResult<T>): AccountResult<T> {
        val session = currentUserSession() ?: run {
            sessionStore.clear()
            mutableState.value = AccountState.SignedOut
            return AccountResult.Failure(AccountFailure.AuthenticationRequired)
        }
        val result = block(session)
        if (result is AccountResult.Failure && result.reason == AccountFailure.AuthenticationRequired) {
            sessionStore.clear()
            mutableState.value = AccountState.SignedOut
        }
        return result
    }

    private fun validUsername(value: String): Boolean {
        val trimmed = value.trim()
        return trimmed.length in 2..64 && trimmed == value && value.none { it.isISOControl() || it == '/' || it == '\\' }
    }
}
