package yun.pixels.client.core.domain.device

import kotlinx.coroutines.flow.Flow

data class ResolvedDevice(
    val device: RemoteDevice,
    val oneTimePassword: String? = null,
)

enum class DeviceResolutionFailure {
    InvalidInput,
    PublicNetworkAddress,
    Unreachable,
    InvalidResponse,
}

sealed interface DeviceResolution {
    data class Success(val resolvedDevice: ResolvedDevice) : DeviceResolution

    data class Failure(val reason: DeviceResolutionFailure) : DeviceResolution
}

interface DeviceResolver {
    suspend fun resolve(connectionInput: String): DeviceResolution
}

interface DeviceDirectory {
    val devices: Flow<List<RemoteDevice>>

    suspend fun save(resolvedDevice: ResolvedDevice)

    suspend fun remove(deviceId: DeviceId)

    suspend fun credential(deviceId: DeviceId): String?
}
