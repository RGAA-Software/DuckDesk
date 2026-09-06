package yun.pixels.client.core.nativebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import px.PxMessage

class RtcDataChannelProtocolTest {
    @Test
    fun tlvMatchesNativeThirtyTwoByteLittleEndianLayout() {
        val payload = byteArrayOf(1, 2, 3, 4)
        val packet = packRtcTlv(payload, 0x0102030405060708L)
        val header = ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN)

        assertEquals(36, packet.size)
        assertEquals(1, header.int)
        assertEquals(4, header.int)
        assertEquals(0, header.int)
        assertEquals(4, header.int)
        assertEquals(0x0102030405060708L, header.long)
        assertEquals(4, header.int)
        assertEquals(0, header.int)
        assertArrayEquals(payload, unpackRtcTlv(packet))
    }

    @Test
    fun rejectsTruncatedOrInconsistentTlv() {
        assertNull(unpackRtcTlv(ByteArray(RTC_TLV_HEADER_BYTES - 1)))
        val packet = packRtcTlv(byteArrayOf(9), 1)
        packet[4] = 2
        assertNull(unpackRtcTlv(packet))
    }

    @Test
    fun helloUsesAndroidIdentityAndRequestedCapabilities() {
        val hello = PxMessage.Message.parseFrom(buildRtcHello("device-a", "stream-b", true, false, true))

        assertEquals(PxMessage.MessageType.kHello, hello.type)
        assertEquals("device-a", hello.deviceId)
        assertEquals("stream-b", hello.streamId)
        assertTrue(hello.hello.enableVideo)
        assertTrue(hello.hello.enableController)
        assertEquals(PxMessage.ClientType.kAndroid, hello.hello.clientType)
    }
}
