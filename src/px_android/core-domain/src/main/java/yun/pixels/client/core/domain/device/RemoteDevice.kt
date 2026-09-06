package yun.pixels.client.core.domain.device

@JvmInline
value class DeviceId(val value: String) {
    init {
        require(value.isNotBlank()) { "DeviceId must not be blank" }
    }
}

enum class DeviceAvailability {
    Online,
    Offline,
    Discovering,
}

data class DeviceEndpoint(
    val host: String,
    val panelPort: Int = DEFAULT_PANEL_PORT,
    val renderPort: Int = DEFAULT_RENDER_PORT,
) {
    init {
        require(host.isNotBlank()) { "Device endpoint host must not be blank" }
        require(panelPort in 1..65535) { "Panel port is outside the valid range" }
        require(renderPort in 1..65535) { "Render port is outside the valid range" }
    }

    companion object {
        const val DEFAULT_PANEL_PORT = 20369
        const val DEFAULT_RENDER_PORT = 20371
    }
}

data class RemoteDevice(
    val id: DeviceId,
    val displayName: String,
    val platformName: String,
    val availability: DeviceAvailability,
    val endpoint: DeviceEndpoint,
    val latencyMillis: Int? = null,
    val lastConnectedEpochMillis: Long? = null,
    val lastSeenEpochMillis: Long? = null,
)
