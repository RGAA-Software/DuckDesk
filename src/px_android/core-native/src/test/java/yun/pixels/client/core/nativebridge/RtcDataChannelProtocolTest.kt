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
    fun rtcCapabilitiesRequireFilePermissionAndReadyChannel() {
        val config = PxMessage.ServerConfiguration.newBuilder().setFileTransferEnabled(true).build()

        assertFalse(config.toRtcSessionCapabilities(false, false, false, setOf("view", "file")).supportsFileTransfer)
        assertFalse(
            config.toRtcSessionCapabilities(false, false, false, setOf("view"), fileTransferReady = true).supportsFileTransfer,
        )
        assertTrue(
            config.toRtcSessionCapabilities(false, false, false, setOf("view", "file"), fileTransferReady = true).supportsFileTransfer,
        )
        assertFalse(
            config.toRtcSessionCapabilities(false, false, true, setOf("view", "clipboard")).supportsClipboardFiles,
        )
        assertFalse(
            config.toRtcSessionCapabilities(
                false,
                false,
                true,
                setOf("view", "clipboard"),
                fileTransferReady = true,
            ).supportsClipboardFiles,
        )
        assertTrue(
            config.toRtcSessionCapabilities(
                false,
                false,
                true,
                setOf("view", "clipboard", "file"),
                fileTransferReady = true,
            ).supportsClipboardFiles,
        )
    }

    @Test
    fun rtcVoiceCapabilityRequiresPermissionProtocolAndNegotiatedVoiceTransport() {
        val supported = PxMessage.ServerConfiguration.newBuilder()
            .setVoiceCallEnabled(true)
            .setVoiceCallProtocolVersion(1)
            .setVoiceCallRequiresHeadset(false)
            .build()

        assertFalse(supported.toRtcSessionCapabilities(true, true, true, setOf("view", "audio")).supportsVoiceCall)
        assertFalse(
            supported.toRtcSessionCapabilities(
                true,
                true,
                true,
                setOf("view"),
                voiceCallReady = true,
            ).supportsVoiceCall,
        )
        val available = supported.toRtcSessionCapabilities(
            true,
            true,
            true,
            setOf("view", "audio"),
            voiceCallReady = true,
        )
        assertTrue(available.supportsVoiceCall)
        assertFalse(available.voiceCallRequiresHeadset)

        val oldProtocol = supported.toBuilder().setVoiceCallProtocolVersion(0).build()
        assertFalse(
            oldProtocol.toRtcSessionCapabilities(
                true,
                true,
                true,
                setOf("view", "audio"),
                voiceCallReady = true,
            ).supportsVoiceCall,
        )
    }

    @Test
    fun rtcClipboardFileProtocolRejectsTypeOnlyMessages() {
        val fileInfo = PxMessage.Message.newBuilder()
            .setType(PxMessage.MessageType.kClipboardInfo)
            .setClipboardInfo(PxMessage.ClipboardInfo.newBuilder().setType(PxMessage.ClipboardType.kClipboardFiles))
            .build()
        val bufferRequest = PxMessage.Message.newBuilder()
            .setType(PxMessage.MessageType.kClipboardReqBuffer)
            .setCpReqBuffer(PxMessage.ClipboardReqBuffer.newBuilder().setFullName("pixels-clipboard://generation/0"))
            .build()
        val typeOnly = PxMessage.Message.newBuilder().setType(PxMessage.MessageType.kClipboardReqBuffer).build()
        val text = PxMessage.Message.newBuilder()
            .setType(PxMessage.MessageType.kClipboardInfo)
            .setClipboardInfo(PxMessage.ClipboardInfo.newBuilder().setType(PxMessage.ClipboardType.kClipboardText))
            .build()

        assertTrue(fileInfo.isRtcClipboardFileProtocolMessage())
        assertTrue(bufferRequest.isRtcClipboardFileProtocolMessage())
        assertFalse(typeOnly.isRtcClipboardFileProtocolMessage())
        assertFalse(text.isRtcClipboardFileProtocolMessage())
    }

    @Test
    fun rtcVoiceControlRejectsMissingOrStaleBodies() {
        val response = PxMessage.Message.newBuilder()
            .setType(PxMessage.MessageType.kVoiceCallResponse)
            .setVoiceCallResponse(
                PxMessage.VoiceCallResponse.newBuilder()
                    .setCallId("call-1")
                    .setRequestId(7)
                    .setAccepted(true),
            )
            .build()
        val hangup = PxMessage.Message.newBuilder()
            .setType(PxMessage.MessageType.kVoiceCallRequest)
            .setVoiceCallRequest(
                PxMessage.VoiceCallRequest.newBuilder()
                    .setCallId("call-1")
                    .setRequestId(7)
                    .setConnect(false),
            )
            .build()

        assertTrue(response.isExpectedRtcVoiceCallResponse("call-1", 7))
        assertFalse(response.isExpectedRtcVoiceCallResponse("call-1", 8))
        assertFalse(response.isExpectedRtcVoiceCallResponse("", 7))
        assertFalse(response.isExpectedRtcVoiceCallResponse("call-1", 0))
        assertFalse(
            PxMessage.Message.newBuilder().setType(PxMessage.MessageType.kVoiceCallResponse).build()
                .isExpectedRtcVoiceCallResponse("call-1", 7),
        )
        assertTrue(hangup.isMatchingRtcVoiceHangup("call-1"))
        assertFalse(hangup.toBuilder().setVoiceCallRequest(hangup.voiceCallRequest.toBuilder().setRequestId(0)).build()
            .isMatchingRtcVoiceHangup("call-1"))
        assertFalse(hangup.toBuilder().setVoiceCallRequest(hangup.voiceCallRequest.toBuilder().setConnect(true)).build()
            .isMatchingRtcVoiceHangup("call-1"))
    }

    @Test
    fun rtcVoiceAudioConfigRequiresTheNegotiatedPcmShape() {
        val compatible = PxMessage.Message.newBuilder()
            .setType(PxMessage.MessageType.kVoiceAudioConfig)
            .setVoiceAudioConfig(
                PxMessage.VoiceAudioConfig.newBuilder()
                    .setCallId("call-2")
                    .setSampleRate(RTC_VOICE_SAMPLE_RATE)
                    .setChannels(RTC_VOICE_CHANNELS)
                    .setFrameMs(RTC_VOICE_FRAME_MILLIS),
            )
            .build()

        assertFalse(compatible.hasIncompatibleRtcVoiceAudioConfig("call-2"))
        assertTrue(
            compatible.toBuilder().setVoiceAudioConfig(compatible.voiceAudioConfig.toBuilder().setChannels(2)).build()
                .hasIncompatibleRtcVoiceAudioConfig("call-2"),
        )
        assertFalse(compatible.hasIncompatibleRtcVoiceAudioConfig("stale-call"))
        assertFalse(
            PxMessage.Message.newBuilder().setType(PxMessage.MessageType.kVoiceAudioConfig).build()
                .hasIncompatibleRtcVoiceAudioConfig("call-2"),
        )
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
