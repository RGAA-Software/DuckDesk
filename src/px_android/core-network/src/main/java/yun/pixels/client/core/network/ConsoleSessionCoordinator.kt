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
    officialEndpoint: String? = null,
) : ConsoleSessionRepository {
    override val officialEndpoint = officialEndpoint?.let(::normalizeEndpoint)
    private val invalidEndpointPolicy = officialEndpoint != null && this.officialEndpoint == null
    private val mutableState = MutableStateFlow<AccountState>(AccountState.Loading)
    private val mutableEndpoint = MutableStateFlow<ConsoleEndpoint?>(null)
    private val endpointMutationMutex = Mutex()
    private var endpointGeneration = 0L
    private val guestMutex = Mutex()
    private var guestSession: GuestSession? = null

    override val state: StateFlow<AccountState> = mutableState.asStateFlow()
    override val endpoint: StateFlow<ConsoleEndpoint?> = mutableEndpoint.asStateFlow()
    override val endpointEditable: Boolean = true

    override suspend fun restore() {
        check(!invalidEndpointPolicy) { "Console endpoint policy is invalid" }
        val storedEndpoint = endpointStore.load()
        val endpoint = storedEndpoint ?: officialEndpoint
        if (storedEndpoint == null && officialEndpoint == null) endpointStore.clear()
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
        if (normalized == officialEndpoint) {
            return AccountResult.Failure(AccountFailure.InvalidEndpoint)
        }
        return activateEndpoint(normalized)
    }

    override suspend fun selectOfficialEndpoint(): AccountResult<ConsoleEndpoint> {
        val selectedEndpoint = officialEndpoint ?: return AccountResult.Failure(AccountFailure.InvalidEndpoint)
        return activateEndpoint(selectedEndpoint)
    }

    private suspend fun activateEndpoint(normalized: ConsoleEndpoint): AccountResult<ConsoleEndpoint> = endpointMutationMutex.withLock {
        if (mutableEndpoint.value != normalized) {
            sessionStore.clear()
            guestMutex.withLock { guestSession = null }
            mutableState.value = AccountState.SignedOut
            mutableEndpoint.value = normalized
            endpointStore.save(normalized)
            endpointGeneration += 1
        }
        AccountResult.Success(normalized)
    }

    override suspend fun testEndpoint(endpoint: String): AccountResult<ConsoleEndpoint> =
        if (normalizeEndpoint(endpoint).let { normalized ->
                normalized == null || normalized == officialEndpoint && mutableEndpoint.value != officialEndpoint
            }
        ) {
            AccountResult.Failure(AccountFailure.InvalidEndpoint)
        } else {
            api.testEndpoint(endpoint)
        }

    override suspend fun login(endpoint: String, username: String, password: String): AccountResult<AccountSession> {
        val saved = if (normalizeEndpoint(endpoint) == officialEndpoint && mutableEndpoint.value == officialEndpoint && officialEndpoint != null) {
            selectOfficialEndpoint()
        } else {
            saveEndpoint(endpoint)
        }
        if (saved is AccountResult.Failure) return saved
        val normalized = (saved as AccountResult.Success).value
        val loginGeneration = endpointMutationMutex.withLock {
            if (mutableEndpoint.value != normalized) return AccountResult.Failure(AccountFailure.InvalidEndpoint)
            mutableState.value = AccountState.Loading
            endpointGeneration
        }
        val result = api.login(normalized.baseUrl, username, password)
        return endpointMutationMutex.withLock {
            if (endpointGeneration != loginGeneration || mutableEndpoint.value != normalized) {
                return@withLock AccountResult.Failure(AccountFailure.InvalidEndpoint)
            }
            when (result) {
                is AccountResult.Success -> {
                    if (result.value.endpoint != normalized) return@withLock AccountResult.Failure(AccountFailure.InvalidResponse)
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
        guestSession?.takeIf { it.endpoint == mutableEndpoint.value }
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

    internal suspend fun currentGuestSession(): GuestSession? = guestMutex.withLock {
        guestSession?.takeIf { session -> session.endpoint == mutableEndpoint.value }
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
