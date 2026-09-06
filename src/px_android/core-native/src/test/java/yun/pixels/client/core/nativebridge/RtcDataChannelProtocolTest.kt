package yun.pixels.client.core.nativebridge

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import px.PxMessage
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayOperation
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayResultState

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

    @Test
    fun virtualDisplayRequestUsesBoundedProductDefaults() {
        val message = buildRtcVirtualDisplayRequest(" request-7 ", RemoteVirtualDisplayOperation.Create)?.build()
        requireNotNull(message)

        assertEquals(PxMessage.MessageType.kVirtualDisplayRequest, message.type)
        assertEquals("request-7", message.virtualDisplayRequest.requestId)
        assertEquals(PxMessage.RemoteVirtualDisplayOperation.kRemoteVirtualDisplayCreate, message.virtualDisplayRequest.operation)
        assertEquals(1920, message.virtualDisplayRequest.width)
        assertEquals(1080, message.virtualDisplayRequest.height)
        assertEquals(60, message.virtualDisplayRequest.refreshHz)
        assertNull(buildRtcVirtualDisplayRequest(" ", RemoteVirtualDisplayOperation.RemoveLast))
        assertNull(buildRtcVirtualDisplayRequest("x".repeat(129), RemoteVirtualDisplayOperation.RemoveLast))
    }

    @Test
    fun virtualDisplayResponseMapsToTypedBoundedResult() {
        val message = PxMessage.Message.newBuilder()
            .setType(PxMessage.MessageType.kVirtualDisplayResponse)
            .setVirtualDisplayResponse(
                PxMessage.VirtualDisplayResponse.newBuilder()
                    .setRequestId("request-8")
                    .setAccepted(true)
                    .setState(PxMessage.VirtualDisplayResponseState.kVirtualDisplayNeedReconnect)
                    .setTopologyChanged(true)
                    .setTopologyGeneration(12)
                    .setOwnedDisplayCount(3)
                    .setErrorMessage("m".repeat(300)),
            )
            .build()

        val result = parseRtcVirtualDisplayResult(message)
        requireNotNull(result)
        assertEquals("request-8", result.requestId)
        assertEquals(RemoteVirtualDisplayResultState.NeedReconnect, result.state)
        assertTrue(result.accepted)
        assertTrue(result.topologyChanged)
        assertEquals(12, result.topologyGeneration)
        assertEquals(3, result.ownedDisplayCount)
        assertEquals(256, result.errorMessage.length)
    }

    @Test
    fun rtcCapabilitiesRequireInputScopeForDisplayManagement() {
        val config = PxMessage.ServerConfiguration.newBuilder()
            .addMonitorsInfo(PxMessage.MonitorInfo.newBuilder().setName(" DISPLAY1 "))
            .setCapturingMonitorName("DISPLAY1")
            .setCanBeOperated(true)
            .setVirtualDisplayEnabled(true)
            .setVirtualDisplayOwnedCount(2)
            .setVirtualDisplayMaxCount(8)
            .setTopologyGeneration(9)
            .build()

        val viewOnly = config.toRtcSessionCapabilities(false, true, false, setOf("view"))
        assertFalse(viewOnly.supportsInput)
        assertFalse(viewOnly.supportsVirtualDisplays)
        val controlled = config.toRtcSessionCapabilities(false, true, false, setOf("view", "input"))
        assertTrue(controlled.supportsInput)
        assertTrue(controlled.supportsVirtualDisplays)
        assertEquals(listOf("DISPLAY1"), controlled.monitorNames)
        assertEquals(2, controlled.ownedVirtualDisplayCount)
        assertEquals(8, controlled.maximumVirtualDisplayCount)
        assertEquals(9, controlled.topologyGeneration)
    }

    @Test
    fun monitorSwitchResponseProducesTypedUpdate() {
        val message = PxMessage.Message.newBuilder()
            .setType(PxMessage.MessageType.kMonitorSwitched)
            .setMonitorSwitched(
                PxMessage.MonitorSwitched.newBuilder()
                    .setName("DISPLAY2")
                    .addMonitorInfo(PxMessage.MonitorInfo.newBuilder().setName("DISPLAY1"))
                    .addMonitorInfo(PxMessage.MonitorInfo.newBuilder().setName("DISPLAY2")),
            )
            .build()

        val update = parseRtcMonitorUpdate(message)
        requireNotNull(update)
        assertEquals("DISPLAY2", update.activeMonitorName)
        assertEquals(listOf("DISPLAY1", "DISPLAY2"), update.monitorNames)
    }
}
