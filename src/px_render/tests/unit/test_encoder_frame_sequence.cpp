#include <gtest/gtest.h>

#include "app/encoder_frame_sequence.h"
#include "px_client_sdk/encoded_video_delivery.h"

namespace px::render {
namespace {

TEST(EncoderFrameSequence, CachedReplayFollowedBySameCaptureDoesNotInvalidateDecoderReference) {
    EncoderFrameSequence sequence{};
    const auto cached_index = sequence.Assign(152);
    const auto live_index = sequence.Assign(152);
    ASSERT_TRUE(cached_index);
    ASSERT_TRUE(live_index);
    media::VideoFrame live_frame{.kind = media::VideoFrameKind::kPredicted, .frame_index = *live_index, .preceding_frame_index = *cached_index};
    const auto delivery = MakeReassembledVideoDelivery(live_frame);
    EXPECT_FALSE(RequiresVideoReferenceReset(delivery.message->video_frame(), cached_index, delivery.dependency));
    live_frame.frame_index = *cached_index;
    const auto duplicate = MakeReassembledVideoDelivery(live_frame);
    EXPECT_TRUE(RequiresVideoReferenceReset(duplicate.message->video_frame(), cached_index, duplicate.dependency));
}

TEST(EncoderFrameSequence, ReplaysSourceRestartAndSkippedCapturesRemainMonotonic) {
    EncoderFrameSequence sequence{};
    EXPECT_EQ(sequence.Assign(151), 151U);
    EXPECT_EQ(sequence.Assign(152), 152U);
    EXPECT_EQ(sequence.Assign(152), 153U);
    EXPECT_EQ(sequence.Assign(153), 154U);
    EXPECT_EQ(sequence.Assign(153), 155U);
    EXPECT_EQ(sequence.Assign(1), 156U);
    EXPECT_EQ(sequence.Assign(400), 400U);
    EXPECT_EQ(sequence.Assign(400), 401U);
    EncoderFrameSequence other_monitor{};
    EXPECT_EQ(other_monitor.Assign(1), 1U);
}

TEST(EncoderFrameSequence, ExhaustionNeverWrapsToAnOldReference) {
    EncoderFrameSequence sequence{};
    EXPECT_EQ(sequence.Assign(std::numeric_limits<std::uint64_t>::max()), std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(sequence.Assign(0));
    EXPECT_FALSE(sequence.Assign(1));
}

}  // namespace
}  // namespace px::render
