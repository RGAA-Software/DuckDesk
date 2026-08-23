#include "voice_call_state.h"
#include "voice_audio_endpoint.h"

#include <gtest/gtest.h>

#include <SDL2/SDL.h>

#include <chrono>
#include <thread>

namespace px {

TEST(VoiceCallStateTest, OutgoingAcceptanceRequiresExactRequest) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call-a", 7, 100));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kOutgoingPending);
    EXPECT_FALSE(state.ApplyResponse("call-a", 8, true));
    EXPECT_FALSE(state.ApplyResponse("call-b", 7, true));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kOutgoingPending);
    EXPECT_TRUE(state.ApplyResponse("call-a", 7, true));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kConnected);
}

TEST(VoiceCallStateTest, RejectedOutgoingRequestReturnsIdle) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call-a", 3, 100));
    EXPECT_TRUE(state.ApplyResponse("call-a", 3, false));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
    EXPECT_TRUE(state.CallId().empty());
}

TEST(VoiceCallStateTest, IncomingCallIsExclusiveAndDuplicateIsRecognized) {
    VoiceCallState state;
    EXPECT_EQ(state.BeginIncoming("call-a", 9, 100), IncomingVoiceCallResult::kPending);
    EXPECT_EQ(state.BeginIncoming("call-a", 9, 101), IncomingVoiceCallResult::kDuplicate);
    EXPECT_EQ(state.BeginIncoming("call-b", 10, 101), IncomingVoiceCallResult::kBusy);
    EXPECT_TRUE(state.AcceptIncoming("call-a", 9));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kConnected);
}

TEST(VoiceCallStateTest, InvalidIncomingRequestDoesNotReserveAudioResources) {
    VoiceCallState state;
    EXPECT_EQ(state.BeginIncoming("", 1, 100), IncomingVoiceCallResult::kInvalid);
    EXPECT_EQ(state.BeginIncoming("call", 0, 100), IncomingVoiceCallResult::kInvalid);
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
}

TEST(VoiceCallStateTest, PendingRequestExpiresAtThirtySeconds) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 5'000));
    EXPECT_FALSE(state.Expire(34'999));
    EXPECT_TRUE(state.Expire(35'000));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
}

TEST(VoiceCallStateTest, MediaRequiresConnectedMatchingCall) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 0));
    EXPECT_FALSE(state.AcceptMedia("call", 1));
    ASSERT_TRUE(state.ApplyResponse("call", 1, true));
    EXPECT_FALSE(state.AcceptMedia("forged", 1));
    EXPECT_TRUE(state.AcceptMedia("call", 1));
}

TEST(VoiceCallStateTest, DuplicateAndOldMediaSequencesAreRejected) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 0));
    ASSERT_TRUE(state.ApplyResponse("call", 1, true));
    EXPECT_TRUE(state.AcceptMedia("call", 100));
    EXPECT_FALSE(state.AcceptMedia("call", 100));
    EXPECT_FALSE(state.AcceptMedia("call", 99));
    EXPECT_TRUE(state.AcceptMedia("call", 101));
}

TEST(VoiceCallStateTest, SequenceComparisonHandlesUint32Wrap) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 0));
    ASSERT_TRUE(state.ApplyResponse("call", 1, true));
    EXPECT_TRUE(state.AcceptMedia("call", 0xfffffffeu));
    EXPECT_TRUE(state.AcceptMedia("call", 1u));
}

TEST(VoiceCallStateTest, WrongCallCannotHangUpActiveCall) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginOutgoing("call", 1, 0));
    ASSERT_TRUE(state.ApplyResponse("call", 1, true));
    EXPECT_FALSE(state.HangUp("old-call"));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kConnected);
    EXPECT_TRUE(state.HangUp("call"));
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
}

TEST(VoiceCallStateTest, CleanupIsIdempotent) {
    VoiceCallState state;
    ASSERT_TRUE(state.BeginIncoming("call", 1, 0) == IncomingVoiceCallResult::kPending);
    state.Reset();
    state.Reset();
    EXPECT_EQ(state.Phase(), VoiceCallPhase::kIdle);
    EXPECT_FALSE(state.HangUp("call"));
}

TEST(VoiceAudioEndpointTest, CaptureEncodeDecodeAndPlayoutRunWithDummyAudioDevice) {
    ASSERT_EQ(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1), 0);

    VoiceAudioEndpoint endpoint;
    std::string error;
    ASSERT_TRUE(endpoint.Start(
        [&endpoint](uint32_t, uint64_t, const std::vector<uint8_t>& opus) {
            endpoint.ReceiveOpus(opus.data(), opus.size());
        }, &error)) << error;

    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    const auto stats = endpoint.Stats();
    endpoint.Stop();

    EXPECT_GT(stats.encoded_packets, 0u);
    EXPECT_GT(stats.decoded_packets, 0u);
}

}  // namespace px
