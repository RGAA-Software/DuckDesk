#include <gtest/gtest.h>

#include <limits>
#include <optional>

#include "decoder_startup_gate.h"
#include "encoded_video_delivery.h"
#include "media_transport/video_recovery_policy.h"

namespace px {
namespace {

TEST(VideoRecoveryPolicy, CoalescesPacketBurstAndRetriesLostRequestsBeforeKeyFrameDeadline) {
    media::VideoRecoveryPolicy recovery{};
    const media::VideoStreamOutput loss{.invalid_reference_frame = 2167, .completed_frame_index = 2169};
    const auto first = recovery.Observe(0, loss, 0);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->kind, media::VideoRecoveryRequestKind::kInvalidateReferences);
    for (std::uint64_t packet_time_us = 1; packet_time_us < 100000; packet_time_us += 100) {
        EXPECT_FALSE(recovery.Observe(0, loss, packet_time_us));
    }
    const auto retry_request = recovery.Observe(0, loss, 100000);
    ASSERT_TRUE(retry_request);
    const std::vector<media::VideoRecoveryRequest> retry{*retry_request};
    ASSERT_EQ(retry.size(), 1U);
    EXPECT_EQ(retry.front().invalid_reference_frame, 2167U);
    const auto escalation_request = recovery.Observe(0, loss, 500000);
    ASSERT_TRUE(escalation_request);
    const std::vector<media::VideoRecoveryRequest> escalation{*escalation_request};
    ASSERT_EQ(escalation.size(), 1U);
    EXPECT_EQ(escalation.front().kind, media::VideoRecoveryRequestKind::kKeyFrame);
    EXPECT_TRUE(recovery.PollDue(600000).empty());
    EXPECT_TRUE(recovery.PollDue(1500000).empty());
    EXPECT_EQ(recovery.PollDue(4500000).size(), 1U);
}

TEST(VideoRecoveryPolicy, CompleteFramesCancelRecoveryAndEachMonitorHasItsOwnReference) {
    media::VideoRecoveryPolicy recovery{};
    media::VideoStreamOutput complete{};
    complete.frame = media::VideoFrame{.kind = media::VideoFrameKind::kIdr, .monitor = "monitor-one"};
    EXPECT_FALSE(recovery.Observe(1, complete, 0));
    complete.frame->monitor = "monitor-two";
    EXPECT_FALSE(recovery.Observe(2, complete, 0));
    const media::VideoStreamOutput loss{.invalid_reference_frame = 42};
    const auto first_monitor = recovery.Observe(1, loss, 100);
    const auto second_monitor = recovery.Observe(2, loss, 100);
    ASSERT_TRUE(first_monitor);
    ASSERT_TRUE(second_monitor);
    EXPECT_EQ(first_monitor->monitor, "monitor-one");
    EXPECT_EQ(second_monitor->monitor, "monitor-two");
    complete.frame->kind = media::VideoFrameKind::kReferenceRecovery;
    EXPECT_FALSE(recovery.Observe(2, complete, 200));
    EXPECT_TRUE(recovery.PollDue(100100).empty());
    const auto retries = recovery.PollDue(4000100);
    ASSERT_EQ(retries.size(), 1U);
    EXPECT_EQ(retries.front().stream, 1);
    recovery.Reset();
    EXPECT_TRUE(recovery.PollDue(10000000).empty());
}

TEST(VideoRecoveryPolicy, MissingInitialReferenceRequestsKeyFrameAndHealthyStaticStreamsRemainIdle) {
    media::VideoRecoveryPolicy recovery{};
    const auto initial = recovery.Observe(0, media::VideoStreamOutput{.needs_idr = true}, 0);
    ASSERT_TRUE(initial);
    EXPECT_EQ(initial->kind, media::VideoRecoveryRequestKind::kKeyFrame);
    EXPECT_FALSE(recovery.Observe(0, media::VideoStreamOutput{.needs_idr = true}, 1000));
    media::VideoStreamOutput complete{};
    complete.frame = media::VideoFrame{.kind = media::VideoFrameKind::kIdr};
    EXPECT_FALSE(recovery.Observe(0, complete, 2000));
    EXPECT_TRUE(recovery.PollDue(30000000).empty());
}

TEST(VideoRecoveryPolicy, IncompleteFrameWaitsForProgressOrSparseStaticSourceWatchdog) {
    media::VideoRecoveryPolicy recovery{};
    const media::VideoStreamOutput partial{.incomplete_frame = true};
    EXPECT_FALSE(recovery.Observe(0, partial, 0));
    EXPECT_TRUE(recovery.PollDue(1000000).empty());
    const auto watchdog = recovery.PollDue(4000000);
    ASSERT_EQ(watchdog.size(), 1U);
    EXPECT_EQ(watchdog.front().kind, media::VideoRecoveryRequestKind::kKeyFrame);
    EXPECT_TRUE(recovery.PollDue(5000000).empty());
    media::VideoStreamOutput complete{};
    complete.frame = media::VideoFrame{.kind = media::VideoFrameKind::kIdr};
    EXPECT_FALSE(recovery.Observe(0, complete, 5000001));
    EXPECT_TRUE(recovery.PollDue(10000000).empty());
}

TEST(VideoRecoveryPolicy, SuccessfulRepairDoesNotDelayKeyFrameForANewLoss) {
    media::VideoRecoveryPolicy recovery{};
    const media::VideoStreamOutput missing_reference{.needs_idr = true};
    ASSERT_TRUE(recovery.Observe(0, missing_reference, 0));
    media::VideoStreamOutput repaired{};
    repaired.frame = media::VideoFrame{.kind = media::VideoFrameKind::kIdr};
    EXPECT_FALSE(recovery.Observe(0, repaired, 20000));
    const auto next_request = recovery.Observe(0, missing_reference, 40000);
    ASSERT_TRUE(next_request);
    EXPECT_EQ(next_request->kind, media::VideoRecoveryRequestKind::kKeyFrame);
}

class VideoDeliveryFixture : public testing::Test {
protected:
    void SetUp() override {
        encoded_frame_.width = 1920;
        encoded_frame_.height = 1080;
        encoded_frame_.monitor = "test-monitor";
        encoded_frame_.encoded.assign(2500, 1);
        packet_parameters_.datagram_size = 1040;
        packet_parameters_.fec_percent = 20;
    }

    std::optional<EncodedVideoDelivery> Deliver(std::uint32_t transport_frame_index, std::uint64_t encoder_frame_index, media::VideoFrameKind kind) {
        packet_parameters_.frame_index = transport_frame_index;
        encoded_frame_.frame_index = encoder_frame_index;
        encoded_frame_.kind = kind;
        const auto packetized = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
        EXPECT_TRUE(packetized);
        if (!packetized) return std::nullopt;
        packet_parameters_.sequence = packetized->next_sequence;
        std::optional<EncodedVideoDelivery> delivery{};
        for (const auto& packet : packetized->packets) {
            const auto datagram = media::ParseMedia(packet);
            EXPECT_TRUE(datagram);
            if (!datagram) continue;
            const auto received = receiver_.Feed(*datagram, 1000000 + transport_frame_index * 16667ULL);
            if (received.frame) delivery = MakeReassembledVideoDelivery(*received.frame);
        }
        return delivery;
    }

    media::VideoFrame encoded_frame_{};
    media::VideoPacketParameters packet_parameters_{};
    media::VideoStreamReceiver receiver_{};
};

TEST_F(VideoDeliveryFixture, RecoveryAfterMissingPacketsPreservesTheDecodedReference) {
    const auto initial = Deliver(1, 4907, media::VideoFrameKind::kIdr);
    ASSERT_TRUE(initial);
    const auto recovery = Deliver(7, 4913, media::VideoFrameKind::kReferenceRecovery);
    ASSERT_TRUE(recovery);
    ASSERT_TRUE(recovery->dependency);
    EXPECT_EQ(recovery->dependency->kind, media::VideoFrameKind::kReferenceRecovery);
    EXPECT_EQ(recovery->dependency->preceding_frame_index, 4907U);
    EXPECT_FALSE(recovery->message->video_frame().key());
    EXPECT_FALSE(RequiresVideoReferenceReset(recovery->message->video_frame(), 4907, recovery->dependency));
    const auto following = Deliver(8, 4914, media::VideoFrameKind::kPredicted);
    ASSERT_TRUE(following);
    EXPECT_FALSE(RequiresVideoReferenceReset(following->message->video_frame(), 4913, following->dependency));
}

TEST_F(VideoDeliveryFixture, PartialSuccessorDoesNotRequestRepairUntilItIsCompletelyAssembled) {
    ASSERT_TRUE(Deliver(1, 100, media::VideoFrameKind::kIdr));
    encoded_frame_.kind = media::VideoFrameKind::kPredicted;
    encoded_frame_.frame_index = 102;
    packet_parameters_.frame_index = 3;
    packet_parameters_.fec_percent = 0;
    const auto packetized = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
    ASSERT_TRUE(packetized);
    ASSERT_GT(packetized->packets.size(), 1U);
    for (std::size_t packet_index{}; packet_index < packetized->packets.size(); ++packet_index) {
        const auto datagram = media::ParseMedia(packetized->packets[packet_index]);
        ASSERT_TRUE(datagram);
        const auto received = receiver_.Feed(*datagram, 1100000 + packet_index);
        EXPECT_FALSE(received.frame);
        if (packet_index + 1 < packetized->packets.size()) {
            EXPECT_FALSE(received.invalid_reference_frame);
            EXPECT_FALSE(received.completed_frame_index);
        } else {
            EXPECT_EQ(received.invalid_reference_frame, 101);
            EXPECT_EQ(received.completed_frame_index, 102);
            EXPECT_EQ(received.completed_bytes, encoded_frame_.encoded.size());
        }
    }
}

TEST_F(VideoDeliveryFixture, TrailingParityAfterCompleteFrameDoesNotArmStaticRecovery) {
    media::VideoRecoveryPolicy recovery{};
    encoded_frame_.kind = media::VideoFrameKind::kIdr;
    packet_parameters_.frame_index = 1;
    const auto packetized = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
    ASSERT_TRUE(packetized);
    for (const auto& packet : packetized->packets) {
        const auto datagram = media::ParseMedia(packet);
        ASSERT_TRUE(datagram);
        EXPECT_FALSE(recovery.Observe(0, receiver_.Feed(*datagram, 1000000), 1000000));
    }
    EXPECT_TRUE(recovery.PollDue(10000000).empty());
}

TEST_F(VideoDeliveryFixture, EncoderIndexCanSkipWithoutAnyTransportFrameLoss) {
    ASSERT_TRUE(Deliver(1, 1000000000000ULL, media::VideoFrameKind::kIdr));
    const auto predicted = Deliver(2, 1000000000042ULL, media::VideoFrameKind::kPredicted);
    ASSERT_TRUE(predicted);
    EXPECT_FALSE(RequiresVideoReferenceReset(predicted->message->video_frame(), 1000000000000ULL, predicted->dependency));
}

TEST_F(VideoDeliveryFixture, DecodeQueueDropCannotBeMistakenForSuccessfulReferenceRecovery) {
    ASSERT_TRUE(Deliver(1, 100, media::VideoFrameKind::kIdr));
    ASSERT_TRUE(Deliver(2, 101, media::VideoFrameKind::kPredicted));
    const auto recovery = Deliver(5, 104, media::VideoFrameKind::kReferenceRecovery);
    ASSERT_TRUE(recovery);
    // Frame 101 was delivered by the network but dropped before decoding.
    EXPECT_TRUE(RequiresVideoReferenceReset(recovery->message->video_frame(), 100, recovery->dependency));
    EXPECT_FALSE(RequiresVideoReferenceReset(recovery->message->video_frame(), 101, recovery->dependency));
}

TEST_F(VideoDeliveryFixture, MissingInitialReferenceRejectsRecoveryAndPredictedFrames) {
    EXPECT_FALSE(Deliver(1, 10, media::VideoFrameKind::kReferenceRecovery));
    EXPECT_FALSE(Deliver(2, 11, media::VideoFrameKind::kPredicted));
    ASSERT_TRUE(Deliver(3, 12, media::VideoFrameKind::kIdr));
    receiver_.Reset();
    EXPECT_FALSE(Deliver(4, 13, media::VideoFrameKind::kReferenceRecovery));
}

TEST_F(VideoDeliveryFixture, RecreatedOrFailedDecoderStillWaitsForConfiguredIdr) {
    ASSERT_TRUE(Deliver(1, 100, media::VideoFrameKind::kIdr));
    const auto recovery = Deliver(3, 102, media::VideoFrameKind::kReferenceRecovery);
    ASSERT_TRUE(recovery);
    DecoderStartupGate startup_gate{};
    const auto now = std::chrono::steady_clock::time_point{};
    EXPECT_EQ(startup_gate.Observe(true, true, now), DecoderStartupGate::Decision::kDecode);
    startup_gate.RequireKeyFrame();
    EXPECT_EQ(startup_gate.Observe(recovery->message->video_frame().key(), false, now), DecoderStartupGate::Decision::kRequestKeyFrame);
    EXPECT_EQ(startup_gate.Observe(true, false, now), DecoderStartupGate::Decision::kWait);
    EXPECT_EQ(startup_gate.Observe(true, true, now), DecoderStartupGate::Decision::kDecode);
}

TEST(VideoReferenceReset, ProtobufDebugTextCannotAuthorizeRecovery) {
    VideoFrame frame{};
    frame.set_frame_index(4913);
    frame.set_extra("udp_synth");
    EXPECT_TRUE(RequiresVideoReferenceReset(frame, 4907, std::nullopt));
    EXPECT_FALSE(RequiresVideoReferenceReset(frame, 4912, std::nullopt));
    const VideoReferenceDependency absent_reference{};
    EXPECT_TRUE(RequiresVideoReferenceReset(frame, 4912, absent_reference));
    const VideoReferenceDependency wrong_reference{.kind = media::VideoFrameKind::kReferenceRecovery, .preceding_frame_index = 1};
    EXPECT_TRUE(RequiresVideoReferenceReset(frame, 4912, wrong_reference));
    EXPECT_TRUE(RequiresVideoReferenceReset(frame, std::numeric_limits<std::uint64_t>::max(), std::nullopt));
}

TEST_F(VideoDeliveryFixture, IndependentStreamsKeepIndependentReferenceChains) {
    ASSERT_TRUE(Deliver(1, 100, media::VideoFrameKind::kIdr));
    encoded_frame_.stream = 1;
    ASSERT_TRUE(Deliver(1, 200, media::VideoFrameKind::kIdr));
    const auto other_stream = Deliver(3, 202, media::VideoFrameKind::kReferenceRecovery);
    ASSERT_TRUE(other_stream);
    EXPECT_EQ(other_stream->dependency->preceding_frame_index, 200U);
    EXPECT_TRUE(RequiresVideoReferenceReset(other_stream->message->video_frame(), 100, other_stream->dependency));
    encoded_frame_.stream = 0;
    const auto first_stream = Deliver(2, 101, media::VideoFrameKind::kPredicted);
    ASSERT_TRUE(first_stream);
    EXPECT_EQ(first_stream->dependency->preceding_frame_index, 100U);
}

}  // namespace
}  // namespace px
