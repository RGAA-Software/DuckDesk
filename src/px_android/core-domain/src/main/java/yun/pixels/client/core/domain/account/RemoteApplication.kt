package yun.pixels.client.core.domain.account

enum class RemoteApplicationType {
    GameHook,
    WebView,
    Rdp,
    Unknown,
}

enum class RemoteApplicationAccess {
    Public,
    Acl,
    Unknown,
}

data class RemoteApplicationInstance(
    val instanceId: String,
    val appId: String = "",
    val state: State,
    val reconnectable: Boolean,
) {
    enum class State { Starting, Running, Stopping, Stopped, Failed }
}

data class RemoteApplication(
    val appId: String,
    val name: String,
    val coverUrl: String,
    val type: RemoteApplicationType,
    val access: RemoteApplicationAccess,
    val version: Long,
    val runningInstance: RemoteApplicationInstance?,
) {
    val isSupported: Boolean
        get() = type == RemoteApplicationType.GameHook || type == RemoteApplicationType.WebView
}

interface ApplicationRepository {
    suspend fun applications(): AccountResult<List<RemoteApplication>>

    suspend fun start(appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance>

    suspend fun stop(instanceId: String): AccountResult<Unit>

    suspend fun resolveConnection(instanceId: String): AccountResult<AccountConnection>
}
