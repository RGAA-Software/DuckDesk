#include "px_render/network/webrtc/remote/rtc_messages.h"

#include <gtest/gtest.h>
#include <array>

#include "px_message.pb.h"

namespace px {
namespace {

std::string SerializeMessage(const MessageType type) {
    Message message;
    message.set_type(type);
    return message.SerializeAsString();
}

std::string SerializeHelloMessage() {
    Message message;
    message.set_type(kHello);
    message.mutable_hello()->set_enable_video(true);
    return message.SerializeAsString();
}

TEST(RtcPayloadAuthorization, RejectsMalformedPayload) {
    EXPECT_FALSE(IsRtcPayloadAuthorized({}, {"view", "input", "clipboard", "file", "audio"}));
    EXPECT_FALSE(IsRtcPayloadAuthorized(std::string{"\x80"}, {"view", "input", "clipboard", "file", "audio"}));
}

TEST(RtcPayloadAuthorization, RequiresInputForInteractiveDisplayMessages) {
    const std::vector<std::string> view_only{"view"};
    const std::vector<std::string> controlled{"view", "input"};

    EXPECT_FALSE(IsRtcPayloadAuthorized(SerializeMessage(kSwitchMonitor), view_only));
    EXPECT_FALSE(IsRtcPayloadAuthorized(SerializeMessage(kVirtualDisplayRequest), view_only));
    EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeMessage(kSwitchMonitor), controlled));
    EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeMessage(kVirtualDisplayRequest), controlled));
    EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeMessage(kVirtualDisplayResponse), view_only));
}

TEST(RtcPayloadAuthorization, KeepsFeaturePermissionsIsolated) {
    EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeHelloMessage(), {"view"}));
    EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeMessage(kServerConfiguration), {"view"}));
    EXPECT_FALSE(IsRtcPayloadAuthorized(SerializeMessage(kClipboardInfo), {"view"}));
    EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeMessage(kClipboardInfo), {"clipboard"}));
    EXPECT_FALSE(IsRtcPayloadAuthorized(SerializeMessage(kFileAction), {"view"}));
    EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeMessage(kFileAction), {"file"}));
    EXPECT_FALSE(IsRtcPayloadAuthorized(SerializeMessage(kVoiceCallRequest), {"view"}));
    EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeMessage(kVoiceCallRequest), {"audio"}));
}

TEST(RtcPayloadAuthorization, NamedInteractiveMessageClassesRequireInput) {
    constexpr std::array interactive{kKeyEvent,
                                     kMouseEvent,
                                     kGamepadState,
                                     kSwitchMonitor,
                                     kSwitchWorkMode,
                                     kChangeMonitorResolution,
                                     kInsertKeyFrame,
                                     kLockDevice,
                                     kStopRender,
                                     kReqCtrlAltDelete,
                                     kUpdateDesktop,
                                     kHardUpdateDesktop,
                                     kSwitchFullColorMode,
                                     kStartMediaRecordClientSide,
                                     kStopMediaRecordClientSide,
                                     kModifyFps,
                                     kVirtualDisplayRequest,
                                     kTextInput,
                                     kApplicationTextCapabilities,
                                     kApplicationTextState,
                                     kApplicationTextSubmit,
                                     kApplicationTextResult,
                                     kApplicationTextBarrier,
                                     kApplicationTextBarrierResult};
    for (const auto type : interactive) {
        SCOPED_TRACE(MessageType_Name(type));
        EXPECT_FALSE(IsRtcPayloadAuthorized(SerializeMessage(type), {"view", "clipboard", "file", "audio"}));
        EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeMessage(type), {"input"}));
    }
}

TEST(RtcPayloadAuthorization, NamedClipboardMessageClassesRequireClipboard) {
    constexpr std::array clipboard{kClipboardInfo,      kClipboardInfoResp, kClipboardReqAtBegin,
                                   kClipboardReqBuffer, kClipboardReqAtEnd, kClipboardRespBuffer};
    for (const auto type : clipboard) {
        SCOPED_TRACE(MessageType_Name(type));
        EXPECT_FALSE(IsRtcPayloadAuthorized(SerializeMessage(type), {"view", "input", "file", "audio"}));
        EXPECT_TRUE(IsRtcPayloadAuthorized(SerializeMessage(type), {"clipboard"}));
    }
}

} // namespace
} // namespace px
