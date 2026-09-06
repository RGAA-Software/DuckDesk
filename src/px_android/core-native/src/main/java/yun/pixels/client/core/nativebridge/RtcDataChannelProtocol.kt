package yun.pixels.client.core.nativebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import px.PxMessage
import yun.pixels.client.core.domain.session.RemoteSessionCapabilities
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayOperation
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayResult
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayResultState

internal fun buildRtcHello(
    deviceId: String,
    streamId: String,
    enableVideo: Boolean,
    enableAudio: Boolean,
    enableInput: Boolean,
): ByteArray = PxMessage.Message.newBuilder()
    .setDeviceId(deviceId)
    .setStreamId(streamId)
    .setType(PxMessage.MessageType.kHello)
    .setHello(
        PxMessage.Hello.newBuilder()
            .setEnableVideo(enableVideo)
            .setEnableAudio(enableAudio)
            .setEnableController(enableInput)
            .setClientType(PxMessage.ClientType.kAndroid)
            .setDeviceName("PixelsAndroid"),
    )
    .build()
    .toByteArray()

internal fun packRtcTlv(payload: ByteArray, packetIndex: Long): ByteArray {
    require(payload.size <= MAX_RTC_TLV_PAYLOAD_BYTES)
    return ByteBuffer.allocate(RTC_TLV_HEADER_BYTES + payload.size)
        .order(ByteOrder.LITTLE_ENDIAN)
        .putInt(RTC_TLV_FULL)
        .putInt(payload.size)
        .putInt(0)
        .putInt(payload.size)
        .putLong(packetIndex)
        .putInt(payload.size)
        .putInt(0)
        .put(payload)
        .array()
}

internal fun unpackRtcTlv(packet: ByteArray): ByteArray? {
    if (packet.size < RTC_TLV_HEADER_BYTES) return null
    val header = ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN)
    if (header.int != RTC_TLV_FULL) return null
    val length = header.int
    val begin = header.int
    val end = header.int
    header.long
    val parentLength = header.int
    header.int
    if (length < 0 || length > MAX_RTC_TLV_PAYLOAD_BYTES || begin != 0 || end != length || parentLength != length) return null
    if (packet.size != RTC_TLV_HEADER_BYTES + length) return null
    return packet.copyOfRange(RTC_TLV_HEADER_BYTES, packet.size)
}

internal fun buildRtcVirtualDisplayRequest(
    requestId: String,
    operation: RemoteVirtualDisplayOperation,
): PxMessage.Message.Builder? {
    val normalizedRequestId = requestId.trim()
    if (normalizedRequestId.isEmpty() || normalizedRequestId.length > MAX_RTC_REQUEST_ID_CHARS) return null
    val nativeOperation = when (operation) {
        RemoteVirtualDisplayOperation.Create -> PxMessage.RemoteVirtualDisplayOperation.kRemoteVirtualDisplayCreate
        RemoteVirtualDisplayOperation.RemoveLast -> PxMessage.RemoteVirtualDisplayOperation.kRemoteVirtualDisplayRemoveLast
    }
    return PxMessage.Message.newBuilder()
        .setType(PxMessage.MessageType.kVirtualDisplayRequest)
        .setVirtualDisplayRequest(
            PxMessage.VirtualDisplayRequest.newBuilder()
                .setRequestId(normalizedRequestId)
                .setOperation(nativeOperation)
                .setWidth(DEFAULT_RTC_VIRTUAL_DISPLAY_WIDTH)
                .setHeight(DEFAULT_RTC_VIRTUAL_DISPLAY_HEIGHT)
                .setRefreshHz(DEFAULT_RTC_VIRTUAL_DISPLAY_REFRESH_HZ),
        )
}

internal fun parseRtcVirtualDisplayResult(message: PxMessage.Message): RemoteVirtualDisplayResult? {
    if (message.type != PxMessage.MessageType.kVirtualDisplayResponse || !message.hasVirtualDisplayResponse()) return null
    val response = message.virtualDisplayResponse
    val requestId = response.requestId.trim()
    if (requestId.isEmpty() || requestId.length > MAX_RTC_REQUEST_ID_CHARS) return null
    val state = when (response.state) {
        PxMessage.VirtualDisplayResponseState.kVirtualDisplayReady -> RemoteVirtualDisplayResultState.Ready
        PxMessage.VirtualDisplayResponseState.kVirtualDisplayNeedReconnect -> RemoteVirtualDisplayResultState.NeedReconnect
        else -> RemoteVirtualDisplayResultState.Failed
    }
    return RemoteVirtualDisplayResult(
        requestId = requestId,
        accepted = response.accepted,
        state = state,
        topologyChanged = response.topologyChanged,
        topologyGeneration = response.topologyGeneration.coerceAtLeast(0),
        ownedDisplayCount = response.ownedDisplayCount.coerceIn(0, MAX_RTC_VIRTUAL_DISPLAY_COUNT),
        errorCode = response.errorCode.take(MAX_RTC_ERROR_CHARS),
        errorMessage = response.errorMessage.take(MAX_RTC_ERROR_CHARS),
    )
}

internal fun PxMessage.ServerConfiguration.toRtcSessionCapabilities(
    enableAudio: Boolean,
    enableInput: Boolean,
    enableClipboard: Boolean,
    permissions: Set<String>,
): RemoteSessionCapabilities {
    val supportsInput = enableInput && canBeOperated && "input" in permissions
    return RemoteSessionCapabilities(
        monitorNames = monitorsInfoList.asSequence()
            .map { monitor -> monitor.name.trim().take(MAX_RTC_MONITOR_NAME_CHARS) }
            .filter(String::isNotEmpty)
            .distinct()
            .take(MAX_RTC_MONITOR_COUNT)
            .toList(),
        activeMonitorName = capturingMonitorName.trim().take(MAX_RTC_MONITOR_NAME_CHARS),
        supportsAudio = enableAudio && audioEnabled,
        supportsInput = supportsInput,
        supportsFileTransfer = false,
        supportsClipboard = enableClipboard && "clipboard" in permissions,
        supportsVirtualDisplays = supportsInput && virtualDisplayEnabled,
        ownedVirtualDisplayCount = virtualDisplayOwnedCount.coerceIn(0, MAX_RTC_VIRTUAL_DISPLAY_COUNT),
        maximumVirtualDisplayCount = virtualDisplayMaxCount.coerceIn(0, MAX_RTC_VIRTUAL_DISPLAY_COUNT),
        topologyGeneration = topologyGeneration.coerceAtLeast(0),
        supportsVoiceCall = false,
    )
}

internal data class RtcMonitorUpdate(val monitorNames: List<String>, val activeMonitorName: String)

internal fun parseRtcMonitorUpdate(message: PxMessage.Message): RtcMonitorUpdate? {
    if (message.type != PxMessage.MessageType.kMonitorSwitched || !message.hasMonitorSwitched()) return null
    val switched = message.monitorSwitched
    val activeMonitorName = switched.name.trim().take(MAX_RTC_MONITOR_NAME_CHARS)
    if (activeMonitorName.isEmpty()) return null
    return RtcMonitorUpdate(
        monitorNames = switched.monitorInfoList.asSequence()
            .map { monitor -> monitor.name.trim().take(MAX_RTC_MONITOR_NAME_CHARS) }
            .filter(String::isNotEmpty)
            .distinct()
            .take(MAX_RTC_MONITOR_COUNT)
            .toList(),
        activeMonitorName = activeMonitorName,
    )
}

internal const val RTC_TLV_HEADER_BYTES = 32
private const val RTC_TLV_FULL = 1
private const val MAX_RTC_TLV_PAYLOAD_BYTES = 4 * 1024 * 1024
private const val MAX_RTC_REQUEST_ID_CHARS = 128
private const val MAX_RTC_ERROR_CHARS = 256
private const val MAX_RTC_MONITOR_NAME_CHARS = 256
private const val MAX_RTC_MONITOR_COUNT = 64
private const val MAX_RTC_VIRTUAL_DISPLAY_COUNT = 64
private const val DEFAULT_RTC_VIRTUAL_DISPLAY_WIDTH = 1920
private const val DEFAULT_RTC_VIRTUAL_DISPLAY_HEIGHT = 1080
private const val DEFAULT_RTC_VIRTUAL_DISPLAY_REFRESH_HZ = 60
