package yun.pixels.client.core.nativebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import px.PxMessage
import yun.pixels.client.core.domain.session.RemoteSessionCapabilities

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

internal fun PxMessage.ServerConfiguration.toRtcSessionCapabilities(
    enableAudio: Boolean,
    enableInput: Boolean,
    enableClipboard: Boolean,
    permissions: Set<String>,
    fileTransferReady: Boolean = false,
    voiceCallReady: Boolean = false,
    recordingReady: Boolean = false,
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
        supportsFileTransfer = fileTransferReady && fileTransferEnabled && "file" in permissions,
        supportsClipboard = enableClipboard && "clipboard" in permissions,
        supportsClipboardFiles = fileTransferReady && enableClipboard && "clipboard" in permissions && "file" in permissions,
        supportsVoiceCall = voiceCallReady && "audio" in permissions && voiceCallEnabled && voiceCallProtocolVersion >= 1,
        voiceCallRequiresHeadset = this.voiceCallRequiresHeadset,
        supportsRecording = recordingReady && "view" in permissions,
    )
}

internal data class RtcMonitorUpdate(val monitorNames: List<String>, val activeMonitorName: String)

internal fun PxMessage.Message.isRtcClipboardFileProtocolMessage(): Boolean = when (type) {
    PxMessage.MessageType.kClipboardInfo -> hasClipboardInfo() && clipboardInfo.type == PxMessage.ClipboardType.kClipboardFiles
    PxMessage.MessageType.kClipboardReqAtBegin -> hasCpReqAtBegin()
    PxMessage.MessageType.kClipboardReqAtEnd -> hasCpReqAtEnd()
    PxMessage.MessageType.kClipboardReqBuffer -> hasCpReqBuffer()
    PxMessage.MessageType.kClipboardRespBuffer -> hasCpRespBuffer()
    else -> false
}

internal fun PxMessage.Message.isExpectedRtcVoiceCallResponse(callId: String, requestId: Long): Boolean =
    type == PxMessage.MessageType.kVoiceCallResponse && hasVoiceCallResponse() && isValidRtcVoiceCallIdentity(callId, requestId) &&
        voiceCallResponse.callId == callId && voiceCallResponse.requestId == requestId

internal fun PxMessage.Message.isMatchingRtcVoiceHangup(callId: String): Boolean =
    type == PxMessage.MessageType.kVoiceCallRequest && hasVoiceCallRequest() && !voiceCallRequest.connect &&
        isValidRtcVoiceCallIdentity(callId, voiceCallRequest.requestId) &&
        voiceCallRequest.callId == callId

internal fun PxMessage.Message.hasIncompatibleRtcVoiceAudioConfig(callId: String): Boolean =
    type == PxMessage.MessageType.kVoiceAudioConfig && hasVoiceAudioConfig() && callId.isNotEmpty() &&
        voiceAudioConfig.callId == callId &&
        (voiceAudioConfig.sampleRate != RTC_VOICE_SAMPLE_RATE || voiceAudioConfig.channels != RTC_VOICE_CHANNELS ||
            voiceAudioConfig.frameMs != RTC_VOICE_FRAME_MILLIS)

internal fun isValidRtcVoiceCallIdentity(callId: String, requestId: Long): Boolean =
    callId.length in 1..MAX_RTC_VOICE_CALL_ID_CHARS && requestId > 0

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
private const val MAX_RTC_MONITOR_NAME_CHARS = 256
private const val MAX_RTC_MONITOR_COUNT = 64
internal const val RTC_VOICE_SAMPLE_RATE = 48_000
internal const val RTC_VOICE_CHANNELS = 1
internal const val RTC_VOICE_FRAME_MILLIS = 20
internal const val MAX_RTC_VOICE_REASON_CHARS = 256
private const val MAX_RTC_VOICE_CALL_ID_CHARS = 128
