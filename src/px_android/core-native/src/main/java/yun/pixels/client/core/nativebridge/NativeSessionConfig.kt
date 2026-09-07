package yun.pixels.client.core.nativebridge

internal data class NativeSessionConfig(
    val sessionId: String,
    val host: String,
    val port: Int,
    val ssl: Boolean,
    val remoteDeviceId: String,
    val displayName: String,
    val streamId: String,
    val clientDeviceId: String,
    val randomPassword: String,
    val connectionTicket: String,
    val connectionNonce: String,
    val connectionTicketDeviceId: String,
    val connectionInstanceId: String,
    val enableVideo: Boolean,
    val enableAudio: Boolean,
    val enableInput: Boolean,
    val enableClipboard: Boolean,
    val preferSoftwareDecoder: Boolean,
)
