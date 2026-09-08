#include "sdk_voice_protocol.h"

#include <gtest/gtest.h>
#include <array>
#include <memory>
#include <set>
#include <thread>

#include "px_voice_call/voice_audio_format.h"

namespace px {

TEST(SdkVoiceProtocol, BuildsCorrelatedConnectRequest) {
    const auto message = MakeVoiceCallRequestMessage("device", "stream", "call", 42, true);
    EXPECT_EQ(message.type(), kVoiceCallRequest);
    EXPECT_EQ(message.device_id(), "device");
    EXPECT_EQ(message.stream_id(), "stream");
    ASSERT_TRUE(message.has_voice_call_request());
    EXPECT_EQ(message.voice_call_request().call_id(), "call");
    EXPECT_EQ(message.voice_call_request().request_id(), 42u);
    EXPECT_TRUE(message.voice_call_request().connect());
}

TEST(SdkVoiceProtocol, HangupRetainsCallAndRequestIdentity) {
    const auto message = MakeVoiceCallRequestMessage("device", "stream", "call", 7, false);
    EXPECT_FALSE(message.voice_call_request().connect());
    EXPECT_EQ(message.voice_call_request().call_id(), "call");
    EXPECT_EQ(message.voice_call_request().request_id(), 7u);
}

TEST(SdkVoiceProtocol, AdvertisesFixedNativeAudioFormat) {
    const auto message = MakeVoiceAudioConfigMessage("d", "s", "c");
    const auto& config = message.voice_audio_config();
    EXPECT_EQ(config.call_id(), "c");
    EXPECT_EQ(config.sample_rate(), VoiceAudioFormat::kSampleRate);
    EXPECT_EQ(config.channels(), VoiceAudioFormat::kChannels);
    EXPECT_EQ(config.frame_ms(), VoiceAudioFormat::kFrameMs);
    EXPECT_EQ(config.bitrate_bps(), VoiceAudioFormat::kBitrateBps);
    EXPECT_TRUE(config.fec());
}

TEST(SdkVoiceProtocol, VoiceFrameIsNotDesktopAudioFrame) {
    const std::vector<uint8_t> opus{1, 2, 3, 4};
    const auto message = MakeVoiceAudioFrameMessage("d", "s", "c", 99, 1234, opus);
    EXPECT_EQ(message.type(), kVoiceAudioFrame);
    EXPECT_FALSE(message.has_audio_frame());
    EXPECT_EQ(message.voice_audio_frame().sequence(), 99u);
    EXPECT_EQ(message.voice_audio_frame().capture_time_ms(), 1234u);
    EXPECT_EQ(message.voice_audio_frame().opus(), std::string("\x01\x02\x03\x04", 4));
}

TEST(SdkVoiceProtocol, NativeRequestIdsAreNeverZeroAndAreUnique) {
    VoiceCallRequestSequence sequence{};
    const auto a = sequence.Next();
    const auto b = sequence.Next();
    EXPECT_NE(a, 0u);
    EXPECT_NE(b, 0u);
    EXPECT_NE(a, b);
}

TEST(SdkVoiceProtocol, RejectionPreservesRequestIdentityAndReason) {
    const auto message = MakeVoiceCallResponseMessage("d", "s", "c", 31, false, "unsupported_direction");
    EXPECT_EQ(message.type(), kVoiceCallResponse);
    EXPECT_EQ(message.device_id(), "d");
    EXPECT_EQ(message.stream_id(), "s");
    EXPECT_EQ(message.voice_call_response().call_id(), "c");
    EXPECT_EQ(message.voice_call_response().request_id(), 31U);
    EXPECT_FALSE(message.voice_call_response().accepted());
    EXPECT_EQ(message.voice_call_response().reason(), "unsupported_direction");
}

TEST(SdkVoiceProtocol, AudioPayloadIsOwnedAndEmptyInputIsSafe) {
    std::vector<std::uint8_t> bytes{1, 2, 3};
    const auto message = MakeVoiceAudioFrameMessage("d", "s", "c", 0, 1, bytes);
    bytes.assign(128, 9);
    EXPECT_EQ(message.voice_audio_frame().opus(), std::string("\x01\x02\x03", 3));
    EXPECT_TRUE(MakeVoiceAudioFrameMessage("d", "s", "c", 1, 2, {}).voice_audio_frame().opus().empty());
}

TEST(SdkVoiceProtocol, RequestSequenceSupportsConcurrentCallersWithoutGlobalState) {
    const auto sequence = std::make_shared<VoiceCallRequestSequence>();
    const auto values = std::make_shared<std::array<std::array<std::uint64_t, 64>, 4>>();
    std::vector<std::jthread> workers{};
    for (std::size_t lane{}; lane < values->size(); ++lane) {
        workers.emplace_back([sequence, values, lane] {
            for (auto& value : (*values)[lane]) {
                value = sequence->Next();
            }
        });
    }
    workers.clear();
    std::set<std::uint64_t> unique{};
    for (const auto& lane : *values) {
        unique.insert(lane.begin(), lane.end());
    }
    EXPECT_EQ(unique.size(), 256U);
    EXPECT_FALSE(unique.contains(0));
}

} // namespace px
