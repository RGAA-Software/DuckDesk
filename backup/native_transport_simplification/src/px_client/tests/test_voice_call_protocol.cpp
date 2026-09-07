#include "ct_voice_call_protocol.h"

#include <gtest/gtest.h>

#include "px_voice_call/voice_audio_endpoint.h"

namespace px {

TEST(VoiceCallProtocolTest, BuildsCorrelatedConnectRequest) {
    const auto message = MakeVoiceCallRequestMessage(
        "device", "stream", "call", 42, true);
    EXPECT_EQ(message.type(), kVoiceCallRequest);
    EXPECT_EQ(message.device_id(), "device");
    EXPECT_EQ(message.stream_id(), "stream");
    ASSERT_TRUE(message.has_voice_call_request());
    EXPECT_EQ(message.voice_call_request().call_id(), "call");
    EXPECT_EQ(message.voice_call_request().request_id(), 42u);
    EXPECT_TRUE(message.voice_call_request().connect());
}

TEST(VoiceCallProtocolTest, HangupRetainsCallAndRequestIdentity) {
    const auto message = MakeVoiceCallRequestMessage(
        "device", "stream", "call", 7, false);
    EXPECT_FALSE(message.voice_call_request().connect());
    EXPECT_EQ(message.voice_call_request().call_id(), "call");
    EXPECT_EQ(message.voice_call_request().request_id(), 7u);
}

TEST(VoiceCallProtocolTest, AdvertisesFixedMvpAudioFormat) {
    const auto message = MakeVoiceAudioConfigMessage("d", "s", "c");
    const auto& config = message.voice_audio_config();
    EXPECT_EQ(config.call_id(), "c");
    EXPECT_EQ(config.sample_rate(), VoiceAudioEndpoint::kSampleRate);
    EXPECT_EQ(config.channels(), VoiceAudioEndpoint::kChannels);
    EXPECT_EQ(config.frame_ms(), VoiceAudioEndpoint::kFrameMs);
    EXPECT_EQ(config.bitrate_bps(), VoiceAudioEndpoint::kBitrateBps);
    EXPECT_TRUE(config.fec());
}

TEST(VoiceCallProtocolTest, VoiceFrameIsNotDesktopAudioFrame) {
    const std::vector<uint8_t> opus{1, 2, 3, 4};
    const auto message = MakeVoiceAudioFrameMessage(
        "d", "s", "c", 99, 1234, opus);
    EXPECT_EQ(message.type(), kVoiceAudioFrame);
    EXPECT_FALSE(message.has_audio_frame());
    EXPECT_EQ(message.voice_audio_frame().sequence(), 99u);
    EXPECT_EQ(message.voice_audio_frame().capture_time_ms(), 1234u);
    EXPECT_EQ(message.voice_audio_frame().opus(), std::string("\x01\x02\x03\x04", 4));
}

TEST(VoiceCallProtocolTest, NativeRequestIdsAreNeverZeroAndAreUnique) {
    const auto a = NextNativeVoiceCallRequestId();
    const auto b = NextNativeVoiceCallRequestId();
    EXPECT_NE(a, 0u);
    EXPECT_NE(b, 0u);
    EXPECT_NE(a, b);
}

}  // namespace px
