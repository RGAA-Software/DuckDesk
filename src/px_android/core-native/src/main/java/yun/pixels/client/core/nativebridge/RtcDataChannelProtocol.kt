package yun.pixels.client.core.nativebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import px.PxMessage

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

internal const val RTC_TLV_HEADER_BYTES = 32
private const val RTC_TLV_FULL = 1
private const val MAX_RTC_TLV_PAYLOAD_BYTES = 4 * 1024 * 1024
