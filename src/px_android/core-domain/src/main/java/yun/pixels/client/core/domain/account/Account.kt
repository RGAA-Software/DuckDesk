package yun.pixels.client.core.domain.account

import kotlinx.coroutines.flow.StateFlow

data class ConsoleEndpoint(val baseUrl: String)

data class GuestSession(
    val endpoint: ConsoleEndpoint,
    val accessToken: String,
    val expiresAtEpochMillis: Long,
)

data class AccountProfile(
    val userId: String,
    val username: String,
    val avatarPath: String?,
    val mustChangePassword: Boolean,
)

data class AccountSession(
    val endpoint: ConsoleEndpoint,
    val profile: AccountProfile,
    val accessToken: String,
    val expiresAtEpochMillis: Long,
)

data class AccountDevice(
    val deviceId: String,
    val displayName: String,
    val online: Boolean,
    val lastSeenEpochMillis: Long?,
)

data class ResourceConnection(
    val host: String,
    val port: Int,
    val remoteResourceId: String,
    val sessionId: String,
    val sessionRevision: Long,
    val frontendToken: String,
    val transport: String,
    val expiresAtEpochMillis: Long,
    val relay: ResourceRelayEndpoint? = null,
    val owner: ResourceConnectionOwner = ResourceConnectionOwner.User,
)

enum class ResourceConnectionOwner {
    User,
    Guest,
}

data class ResourceRelayEndpoint(
    val host: String,
    val port: Int,
    val admissionTicket: String,
)

enum class AccountFailure {
    InvalidEndpoint,
    UntrustedDeployment,
    InvalidCredentials,
    AuthenticationRequired,
    Forbidden,
    RateLimited,
    DeviceOffline,
    NotFound,
    NetworkUnavailable,
    InvalidResponse,
    ServerError,
    UsernameConflict,
    QuotaExceeded,
    InstanceBusy,
    UnsupportedApplication,
    AccountCreatedLoginFailed,
}

sealed interface AccountResult<out T> {
    data class Success<T>(val value: T) : AccountResult<T>

    data class Failure(val reason: AccountFailure) : AccountResult<Nothing>
}

sealed interface AccountState {
    data object SignedOut : AccountState

    data object Loading : AccountState

    data class SignedIn(val session: AccountSession) : AccountState
}

interface AccountSessionStore {
    suspend fun load(): AccountSession?

    suspend fun save(session: AccountSession)

    suspend fun clear()
}

interface ConsoleEndpointStore {
    suspend fun load(): ConsoleEndpoint?

    suspend fun save(endpoint: ConsoleEndpoint)

    suspend fun clear()
}

interface AccountRepository {
    val state: StateFlow<AccountState>

    suspend fun restore()

    suspend fun login(endpoint: String, username: String, password: String): AccountResult<AccountSession>

    suspend fun logout(): AccountResult<Unit>

    suspend fun devices(): AccountResult<List<AccountDevice>>

    suspend fun resolveConnection(deviceId: String): AccountResult<ResourceConnection>
}

interface ConsoleSessionRepository : AccountRepository {
    val endpoint: StateFlow<ConsoleEndpoint?>
    val endpointEditable: Boolean
        get() = true

    suspend fun saveEndpoint(endpoint: String): AccountResult<ConsoleEndpoint>

    suspend fun testEndpoint(endpoint: String): AccountResult<ConsoleEndpoint>

    suspend fun register(username: String, password: String): AccountResult<AccountSession>
}
