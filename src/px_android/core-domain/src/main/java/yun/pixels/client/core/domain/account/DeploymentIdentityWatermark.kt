package yun.pixels.client.core.domain.account

data class DeploymentIdentityWatermark(
    val deploymentId: String,
    val deploymentKind: String,
    val certificateVersion: Long,
    val descriptorRevision: Long,
    val trustEpoch: Long,
)

sealed interface DeploymentIdentityWatermarkState {
    data object Empty : DeploymentIdentityWatermarkState

    data class Present(val watermark: DeploymentIdentityWatermark) : DeploymentIdentityWatermarkState

    data object Invalid : DeploymentIdentityWatermarkState
}

interface DeploymentIdentityWatermarkStore {
    fun load(): DeploymentIdentityWatermarkState

    fun save(watermark: DeploymentIdentityWatermark): Boolean
}
