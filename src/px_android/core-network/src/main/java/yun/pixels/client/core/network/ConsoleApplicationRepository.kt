package yun.pixels.client.core.network

import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountRepository
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationInstance

interface ConsoleApplicationApi {
    suspend fun applications(session: AccountSession): AccountResult<List<RemoteApplication>>

    suspend fun startApplication(
        session: AccountSession,
        appId: String,
        clientNonce: String,
    ): AccountResult<RemoteApplicationInstance>

    suspend fun stopApplication(session: AccountSession, instanceId: String): AccountResult<Unit>
}

class ConsoleApplicationRepository(
    private val api: ConsoleApplicationApi,
    private val accountRepository: AccountRepository,
    private val now: () -> Long = System::currentTimeMillis,
) : ApplicationRepository {
    override suspend fun applications(): AccountResult<List<RemoteApplication>> = withSession(api::applications)

    override suspend fun start(appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance> =
        withSession { session -> api.startApplication(session, appId, clientNonce) }

    override suspend fun stop(instanceId: String): AccountResult<Unit> =
        withSession { session -> api.stopApplication(session, instanceId) }

    private suspend fun <T> withSession(block: suspend (AccountSession) -> AccountResult<T>): AccountResult<T> {
        val session = (accountRepository.state.value as? AccountState.SignedIn)?.session
            ?.takeIf { it.expiresAtEpochMillis > now() }
            ?: return AccountResult.Failure(AccountFailure.AuthenticationRequired)
        return block(session)
    }
}
