#include "px_render/network/webrtc/remote/rtc_messages.h"

#include <gtest/gtest.h>

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

} // namespace
} // namespace px
