package yun.pixels.client.core.network

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountRepository
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountSessionStore
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.JoinMode

class ConsoleAccountRepository(
    private val api: ConsoleAccountApi,
    private val sessionStore: AccountSessionStore,
    private val now: () -> Long = System::currentTimeMillis,
) : AccountRepository {
    private val mutableState = MutableStateFlow<AccountState>(AccountState.Loading)
    override val state: StateFlow<AccountState> = mutableState.asStateFlow()

    override suspend fun restore() {
        val session = sessionStore.load()?.takeIf { it.expiresAtEpochMillis > now() }
        mutableState.value = if (session == null) AccountState.SignedOut else AccountState.SignedIn(session)
        if (session == null) sessionStore.clear()
    }

    override suspend fun login(endpoint: String, username: String, password: String): AccountResult<AccountSession> {
        mutableState.value = AccountState.Loading
        return when (val result = api.login(endpoint, username, password)) {
            is AccountResult.Success -> {
                sessionStore.save(result.value)
                mutableState.value = AccountState.SignedIn(result.value)
                result
            }
            is AccountResult.Failure -> {
                mutableState.value = AccountState.SignedOut
                result
            }
        }
    }

    override suspend fun logout(): AccountResult<Unit> {
        val session = currentSession() ?: return AccountResult.Success(Unit)
        val result = api.logout(session)
        sessionStore.clear()
        mutableState.value = AccountState.SignedOut
        return result
    }

    override suspend fun devices(): AccountResult<List<AccountDevice>> = withSession { session -> api.devices(session) }

    override suspend fun issueTicket(
        deviceId: String,
        clientNonce: String,
        joinMode: JoinMode,
    ): AccountResult<ConnectionTicket> = withSession { session -> api.issueTicket(session, deviceId, clientNonce, joinMode) }

    private fun currentSession(): AccountSession? = (mutableState.value as? AccountState.SignedIn)?.session?.takeIf {
        it.expiresAtEpochMillis > now()
    }

    private suspend fun <T> withSession(block: suspend (AccountSession) -> AccountResult<T>): AccountResult<T> {
        val session = currentSession() ?: run {
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
}
