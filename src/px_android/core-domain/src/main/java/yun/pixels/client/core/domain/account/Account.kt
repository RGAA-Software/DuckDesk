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
    val absoluteExpiresAtEpochMillis: Long,
)

data class AccountDevice(
    val deviceId: String,
    val displayName: String,
    val online: Boolean,
    val lastSeenEpochMillis: Long?,
)

data class AccountConnection(
    val host: String,
    val port: Int,
    val deviceId: String,
    val instanceId: String,
    val passwordHash: String,
    val relayHost: String,
    val relayPort: Int,
    val signalDeviceId: String,
    val appType: RemoteApplicationType? = null,
)

enum class AccountFailure {
    InvalidEndpoint,
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

    suspend fun resolveConnection(deviceId: String): AccountResult<AccountConnection>
}

interface ConsoleSessionRepository : AccountRepository {
    val endpoint: StateFlow<ConsoleEndpoint?>

    suspend fun saveEndpoint(endpoint: String): AccountResult<ConsoleEndpoint>

    suspend fun testEndpoint(endpoint: String): AccountResult<ConsoleEndpoint>

    suspend fun register(username: String, password: String): AccountResult<AccountSession>
}
