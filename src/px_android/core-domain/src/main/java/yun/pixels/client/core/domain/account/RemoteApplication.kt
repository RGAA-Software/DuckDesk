package yun.pixels.client.core.domain.account

data class RemoteApplicationInstance(
    val instanceId: String,
    val state: State,
    val reconnectable: Boolean,
) {
    enum class State { Starting, Running, Stopping, Stopped, Failed }
}

data class RemoteApplication(
    val appId: String,
    val name: String,
    val coverUrl: String,
    val runningInstance: RemoteApplicationInstance?,
)

interface ApplicationRepository {
    suspend fun applications(): AccountResult<List<RemoteApplication>>

    suspend fun start(appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance>

    suspend fun stop(instanceId: String): AccountResult<Unit>
}
