package yun.pixels.client.core.network

import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.ResourceConnection
import yun.pixels.client.core.domain.account.ResourceConnectionOwner
import yun.pixels.client.core.domain.session.RemoteResourceConnectionRenewal
import yun.pixels.client.core.domain.session.RemoteResourceConnectionRenewer
import yun.pixels.client.core.domain.session.RemoteSessionFailure
import yun.pixels.client.core.domain.session.RemoteSessionTarget

class ConsoleResourceConnectionRenewer(
    private val accountApi: ConsoleAccountApi,
    private val applicationApi: ConsoleApplicationApi,
    private val sessions: ConsoleSessionCoordinator,
) : RemoteResourceConnectionRenewer {
    override suspend fun renew(target: RemoteSessionTarget): RemoteResourceConnectionRenewal {
        val result = when (target) {
            is RemoteSessionTarget.Direct -> return RemoteResourceConnectionRenewal.Rejected(RemoteSessionFailure.InvalidRequest)
            is RemoteSessionTarget.Account -> renewAccountConnection(target)
            is RemoteSessionTarget.CloudApplication -> renewCloudApplicationConnection(target)
        }
        return result.toRemoteRenewal()
    }

    private suspend fun renewAccountConnection(target: RemoteSessionTarget.Account) = when (target.connection.owner) {
        ResourceConnectionOwner.User -> sessions.currentUserSession()?.let { session ->
            accountApi.renewConnection(session, target.remoteDeviceId, target.connection)
        } ?: AccountResult.Failure(AccountFailure.AuthenticationRequired)
        ResourceConnectionOwner.Guest -> AccountResult.Failure(AccountFailure.Forbidden)
    }

    private suspend fun renewCloudApplicationConnection(target: RemoteSessionTarget.CloudApplication) = when (target.connection.owner) {
        ResourceConnectionOwner.User -> sessions.currentUserSession()?.let { session ->
            applicationApi.renewApplicationConnection(session, target.appId, target.instanceId, target.connection)
        } ?: AccountResult.Failure(AccountFailure.AuthenticationRequired)
        ResourceConnectionOwner.Guest -> sessions.currentGuestSession()?.let { session ->
            applicationApi.renewGuestApplicationConnection(session, target.appId, target.instanceId, target.connection)
        } ?: AccountResult.Failure(AccountFailure.AuthenticationRequired)
    }
}

private fun AccountResult<ResourceConnection>.toRemoteRenewal(): RemoteResourceConnectionRenewal =
    when (this) {
        is AccountResult.Success -> RemoteResourceConnectionRenewal.Renewed(value)
        is AccountResult.Failure -> RemoteResourceConnectionRenewal.Rejected(reason.toRemoteSessionFailure())
    }

private fun AccountFailure.toRemoteSessionFailure(): RemoteSessionFailure = when (this) {
    AccountFailure.AuthenticationRequired,
    AccountFailure.InvalidCredentials,
    AccountFailure.Forbidden,
    AccountFailure.UntrustedDeployment,
    -> RemoteSessionFailure.AuthenticationRejected
    AccountFailure.DeviceOffline -> RemoteSessionFailure.DeviceOffline
    AccountFailure.NetworkUnavailable,
    AccountFailure.RateLimited,
    AccountFailure.ServerError,
    -> RemoteSessionFailure.NetworkUnavailable
    AccountFailure.InvalidEndpoint,
    AccountFailure.InvalidResponse,
    AccountFailure.UnsupportedApplication,
    -> RemoteSessionFailure.ProtocolError
    AccountFailure.NotFound -> RemoteSessionFailure.RemoteEnded
    AccountFailure.UsernameConflict,
    AccountFailure.QuotaExceeded,
    AccountFailure.InstanceBusy,
    AccountFailure.AccountCreatedLoginFailed,
    -> RemoteSessionFailure.TransportUnavailable
}
