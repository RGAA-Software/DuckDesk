package yun.pixels.client.core.network

import yun.pixels.client.core.domain.account.AccountConnection
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.GuestSession
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationInstance

interface ConsoleApplicationApi {
    suspend fun applications(session: AccountSession): AccountResult<List<RemoteApplication>>

    suspend fun startApplication(session: AccountSession, appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance>

    suspend fun stopApplication(session: AccountSession, instanceId: String): AccountResult<Unit>

    suspend fun resolveApplicationConnection(session: AccountSession, instanceId: String): AccountResult<AccountConnection>

    suspend fun publicApplications(endpoint: ConsoleEndpoint): AccountResult<List<RemoteApplication>>

    suspend fun guestInstances(session: GuestSession): AccountResult<List<RemoteApplicationInstance>>

    suspend fun startGuestApplication(session: GuestSession, appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance>

    suspend fun stopGuestApplication(session: GuestSession, instanceId: String): AccountResult<Unit>

    suspend fun resolveGuestApplicationConnection(session: GuestSession, instanceId: String): AccountResult<AccountConnection>
}

class ConsoleApplicationRepository(
    private val api: ConsoleApplicationApi,
    private val sessions: ConsoleSessionCoordinator,
) : ApplicationRepository {
    override suspend fun applications(): AccountResult<List<RemoteApplication>> {
        sessions.currentUserSession()?.let { return api.applications(it) }
        return withGuestRetry { guest ->
            when (val catalog = api.publicApplications(guest.endpoint)) {
                is AccountResult.Failure -> catalog
                is AccountResult.Success -> when (val instances = api.guestInstances(guest)) {
                    is AccountResult.Failure -> instances
                    is AccountResult.Success -> AccountResult.Success(mergeGuestInstances(catalog.value, instances.value))
                }
            }
        }
    }

    override suspend fun start(appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance> {
        sessions.currentUserSession()?.let { return api.startApplication(it, appId, clientNonce) }
        return withGuestRetry { guest -> api.startGuestApplication(guest, appId, clientNonce) }
    }

    override suspend fun stop(instanceId: String): AccountResult<Unit> {
        sessions.currentUserSession()?.let { return api.stopApplication(it, instanceId) }
        return withGuestRetry { guest -> api.stopGuestApplication(guest, instanceId) }
    }

    override suspend fun resolveConnection(instanceId: String): AccountResult<AccountConnection> {
        sessions.currentUserSession()?.let { return validateConnection(api.resolveApplicationConnection(it, instanceId)) }
        return withGuestRetry { guest -> validateConnection(api.resolveGuestApplicationConnection(guest, instanceId)) }
    }

    private fun validateConnection(result: AccountResult<AccountConnection>): AccountResult<AccountConnection> = when (result) {
        is AccountResult.Failure -> result
        is AccountResult.Success -> if (result.value.appType?.let { type ->
                type == yun.pixels.client.core.domain.account.RemoteApplicationType.GameHook ||
                    type == yun.pixels.client.core.domain.account.RemoteApplicationType.WebView
            } == true
        ) {
            result
        } else {
            AccountResult.Failure(AccountFailure.UnsupportedApplication)
        }
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

internal fun mergeGuestInstances(
    applications: List<RemoteApplication>,
    instances: List<RemoteApplicationInstance>,
): List<RemoteApplication> {
    val activeStates = setOf(
        RemoteApplicationInstance.State.Starting,
        RemoteApplicationInstance.State.Running,
        RemoteApplicationInstance.State.Stopping,
    )
    val activeByApp = instances
        .filter { it.appId.isNotBlank() && it.state in activeStates }
        .associateBy(RemoteApplicationInstance::appId)
    return applications.map { application -> application.copy(runningInstance = activeByApp[application.appId]) }
}
