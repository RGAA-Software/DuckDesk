package yun.pixels.client.core.network

import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.account.GuestSession
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import yun.pixels.client.core.domain.account.ResourceConnection

interface ConsoleApplicationApi {
    suspend fun applications(session: AccountSession): AccountResult<List<RemoteApplication>>

    suspend fun startApplication(session: AccountSession, appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance>

    suspend fun stopApplication(session: AccountSession, instanceId: String): AccountResult<Unit>

    suspend fun resolveApplicationConnection(
        session: AccountSession,
        appId: String,
        instanceId: String,
    ): AccountResult<ResourceConnection>

    suspend fun publicApplications(session: GuestSession): AccountResult<List<RemoteApplication>>

    suspend fun startGuestApplication(session: GuestSession, appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance>

    suspend fun stopGuestApplication(session: GuestSession, instanceId: String): AccountResult<Unit>

    suspend fun resolveGuestApplicationConnection(
        session: GuestSession,
        appId: String,
        instanceId: String,
    ): AccountResult<ResourceConnection>
}

class ConsoleApplicationRepository(
    private val api: ConsoleApplicationApi,
    private val sessions: ConsoleSessionCoordinator,
) : ApplicationRepository {
    override suspend fun applications(): AccountResult<List<RemoteApplication>> {
        sessions.currentUserSession()?.let { return api.applications(it) }
        return withGuestRetry(api::publicApplications)
    }

    override suspend fun start(appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance> {
        sessions.currentUserSession()?.let { return api.startApplication(it, appId, clientNonce) }
        return withGuestRetry { guest -> api.startGuestApplication(guest, appId, clientNonce) }
    }

    override suspend fun stop(instanceId: String): AccountResult<Unit> {
        sessions.currentUserSession()?.let { return api.stopApplication(it, instanceId) }
        return withGuestRetry { guest -> api.stopGuestApplication(guest, instanceId) }
    }

    override suspend fun resolveConnection(appId: String, instanceId: String): AccountResult<ResourceConnection> {
        sessions.currentUserSession()?.let { return api.resolveApplicationConnection(it, appId, instanceId) }
        return withGuestRetry { guest -> api.resolveGuestApplicationConnection(guest, appId, instanceId) }
    }

    private suspend fun <T> withGuestRetry(block: suspend (GuestSession) -> AccountResult<T>): AccountResult<T> {
        repeat(2) { attempt ->
            val guest = when (val result = sessions.guestSession()) {
                is AccountResult.Success -> result.value
                is AccountResult.Failure -> return result
            }
            val result = block(guest)
            if (result !is AccountResult.Failure || result.reason != AccountFailure.AuthenticationRequired || attempt == 1) return result
            sessions.invalidateGuest()
        }
        return AccountResult.Failure(AccountFailure.AuthenticationRequired)
    }
}
