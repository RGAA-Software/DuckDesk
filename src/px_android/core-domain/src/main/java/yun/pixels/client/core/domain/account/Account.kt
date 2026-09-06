package yun.pixels.client.core.domain.account

import kotlinx.coroutines.flow.StateFlow

data class ConsoleEndpoint(val baseUrl: String)

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

data class ConnectionTicket(
    val ticket: String,
    val renewalToken: String,
    val launchUrl: String,
    val expiresAtEpochMillis: Long,
    val logicalSessionId: String,
    val streamId: String,
    val joinMode: JoinMode,
    val permissions: Set<String>,
    val rtcIceConfigJson: String,
    val relayHost: String,
    val relayPort: Int,
    val signalDeviceId: String,
)

enum class JoinMode {
    Control,
    Observe,
}

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

interface AccountRepository {
    val state: StateFlow<AccountState>

    suspend fun restore()

    suspend fun login(endpoint: String, username: String, password: String): AccountResult<AccountSession>

    suspend fun logout(): AccountResult<Unit>

    suspend fun devices(): AccountResult<List<AccountDevice>>

    suspend fun issueTicket(deviceId: String, clientNonce: String, joinMode: JoinMode): AccountResult<ConnectionTicket>

    suspend fun renewTicket(ticket: ConnectionTicket, clientNonce: String): AccountResult<ConnectionTicket>
}
