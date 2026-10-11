#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <optional>

#include "decoder_startup_gate.h"
#include "decoder_output_progress.h"
#include "encoded_video_delivery.h"
#include "media_transport/video_recovery_policy.h"

namespace px {
namespace {

TEST(DecoderOutputRecovery, OrdinaryDelayedOutputDoesNotResetDecoder) {
    DecoderOutputProgress output_progress{};
    const auto started = DecoderOutputProgress::Clock::time_point{};
    EXPECT_FALSE(output_progress.Observe(false, started));
    EXPECT_FALSE(output_progress.Observe(false, started + std::chrono::milliseconds{17}));
    EXPECT_FALSE(output_progress.Observe(true, started + std::chrono::milliseconds{34}));
    EXPECT_FALSE(output_progress.Observe(false, started + std::chrono::seconds{5}));
    EXPECT_FALSE(output_progress.Observe(true, started + std::chrono::seconds{5} + std::chrono::milliseconds{17}));
}

TEST(DecoderOutputRecovery, AcceptedInputsWithoutImagesRequestConfiguredIdrInsteadOfWaitingFourSeconds) {
    DecoderOutputProgress output_progress{};
    DecoderStartupGate startup_gate{};
    const auto started = DecoderOutputProgress::Clock::time_point{};
    ASSERT_EQ(startup_gate.Observe(true, true, started), DecoderStartupGate::Decision::kDecode);
    ASSERT_FALSE(output_progress.Observe(true, started));
    for (int frame_offset{1}; frame_offset <= 6; ++frame_offset) {
        EXPECT_FALSE(output_progress.Observe(false, started + std::chrono::milliseconds{17 * frame_offset}));
    }
    const auto recovery_at = started + std::chrono::milliseconds{119};
    ASSERT_TRUE(output_progress.Observe(false, recovery_at));
    output_progress.Reset();
    startup_gate.RequireKeyFrame(recovery_at);
    EXPECT_EQ(startup_gate.Observe(false, false, recovery_at + std::chrono::milliseconds{17}), DecoderStartupGate::Decision::kWait);
    EXPECT_EQ(startup_gate.Observe(true, false, recovery_at + std::chrono::milliseconds{34}), DecoderStartupGate::Decision::kWait);
    EXPECT_EQ(startup_gate.Observe(false, false, recovery_at + std::chrono::seconds{1}), DecoderStartupGate::Decision::kRequestKeyFrame);
    EXPECT_EQ(startup_gate.Observe(true, true, recovery_at + std::chrono::milliseconds{1017}), DecoderStartupGate::Decision::kDecode);
    EXPECT_FALSE(output_progress.Observe(true, recovery_at + std::chrono::milliseconds{1017}));
}

TEST(DecoderOutputRecovery, NetworkSilenceAndRecreatedDecodersDoNotInheritOutputDebt) {
    DecoderOutputProgress first_monitor{};
    DecoderOutputProgress second_monitor{};
    const auto started = DecoderOutputProgress::Clock::time_point{};
    EXPECT_FALSE(first_monitor.Observe(false, started));
    EXPECT_FALSE(first_monitor.Observe(false, started + std::chrono::seconds{4}));
    EXPECT_FALSE(first_monitor.Observe(false, started + std::chrono::seconds{5}));
    EXPECT_FALSE(second_monitor.Observe(false, started + std::chrono::seconds{6}));
    first_monitor.Reset();
    EXPECT_FALSE(first_monitor.Observe(false, started + std::chrono::seconds{6}));
    EXPECT_FALSE(first_monitor.Observe(true, started + std::chrono::seconds{6} + std::chrono::milliseconds{17}));
}

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

TEST(VideoRecoveryPolicy, KnownReferenceRepairsAnIncompleteFinalFrameWithoutWaitingForAnotherCompleteFrame) {
    media::VideoRecoveryPolicy recovery{};
    const media::VideoStreamOutput established{
        .frame = media::VideoFrame{.kind = media::VideoFrameKind::kIdr, .frame_index = 18116, .monitor = "game-hook"}};
    EXPECT_FALSE(recovery.Observe(0, established, 1000000));
    EXPECT_FALSE(recovery.Observe(0, media::VideoStreamOutput{.incomplete_frame = true}, 1010000));
    EXPECT_TRUE(recovery.PollDue(1109999).empty());
    const auto requests = recovery.PollDue(1110000);
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_EQ(requests.front().kind, media::VideoRecoveryRequestKind::kInvalidateReferences);
    EXPECT_EQ(requests.front().invalid_reference_frame, 18117);
    EXPECT_EQ(requests.front().monitor, "game-hook");
    // One repair is enough while no new frame arrives; do not flood a stalled path.
    EXPECT_TRUE(recovery.PollDue(1210000).empty());
    EXPECT_TRUE(recovery.PollDue(2010000).empty());
    const media::VideoStreamOutput repaired{
        .frame = media::VideoFrame{.kind = media::VideoFrameKind::kReferenceRecovery, .frame_index = 18122, .monitor = "game-hook"}};
    EXPECT_FALSE(recovery.Observe(0, repaired, 2020000));
    EXPECT_TRUE(recovery.PollDue(9000000).empty());
}

TEST(VideoRecoveryPolicy, ActivePartialDatagramsDoNotInvalidateAHealthyLargeFrame) {
    media::VideoRecoveryPolicy recovery{};
    const media::VideoStreamOutput established{.frame = media::VideoFrame{.kind = media::VideoFrameKind::kIdr, .frame_index = 900}};
    EXPECT_FALSE(recovery.Observe(0, established, 1000000));
    for (std::uint64_t offset_us{}; offset_us <= 700000; offset_us += 50000) {
        EXPECT_FALSE(recovery.Observe(0, media::VideoStreamOutput{.incomplete_frame = true}, 1010000 + offset_us));
        EXPECT_TRUE(recovery.PollDue(1035000 + offset_us).empty());
    }
    const auto requests = recovery.PollDue(1810000);
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_EQ(requests.front().kind, media::VideoRecoveryRequestKind::kInvalidateReferences);
    EXPECT_EQ(requests.front().invalid_reference_frame, 901);
}

TEST(VideoRecoveryPolicy, ReliableRelayPartialPauseWaitsForTheOriginalFrameWithoutSpeculativeRepair) {
    media::VideoRecoveryPolicy recovery{};
    recovery.SetPartialIdleRepairEnabled(false);
    const media::VideoStreamOutput established{
        .frame = media::VideoFrame{.kind = media::VideoFrameKind::kPredicted, .frame_index = 722, .monitor = "game-hook"}};
    EXPECT_FALSE(recovery.Observe(0, established, 1000000));
    EXPECT_FALSE(recovery.Observe(0, media::VideoStreamOutput{.incomplete_frame = true}, 1010000));
    EXPECT_TRUE(recovery.PollDue(1112147).empty());
    const media::VideoStreamOutput resumed{
        .frame = media::VideoFrame{.kind = media::VideoFrameKind::kPredicted, .frame_index = 723, .monitor = "game-hook"}};
    EXPECT_FALSE(recovery.Observe(0, resumed, 1195000));
    EXPECT_TRUE(recovery.PollDue(6000000).empty());
}

TEST(VideoRecoveryPolicy, ReliableRelayStillRepairsRealReferenceLossAndDirectPathRestoresThePartialDeadline) {
    media::VideoRecoveryPolicy recovery{};
    recovery.SetPartialIdleRepairEnabled(false);
    const media::VideoStreamOutput established{
        .frame = media::VideoFrame{.kind = media::VideoFrameKind::kIdr, .frame_index = 900, .monitor = "game-hook"}};
    EXPECT_FALSE(recovery.Observe(0, established, 1000000));
    const auto lost = recovery.Observe(0, media::VideoStreamOutput{.invalid_reference_frame = 901}, 1100000);
    ASSERT_TRUE(lost);
    EXPECT_EQ(lost->kind, media::VideoRecoveryRequestKind::kInvalidateReferences);
    EXPECT_EQ(lost->invalid_reference_frame, 901);
    EXPECT_FALSE(recovery.Observe(0, established, 1200000));
    EXPECT_FALSE(recovery.Observe(0, media::VideoStreamOutput{.incomplete_frame = true}, 1210000));
    EXPECT_TRUE(recovery.PollDue(1410000).empty());
    recovery.SetPartialIdleRepairEnabled(true);
    ASSERT_EQ(recovery.PollDue(1410001).size(), 1U);
    EXPECT_FALSE(recovery.Observe(0, established, 1500000));
    recovery.SetPartialIdleRepairEnabled(false);
    EXPECT_FALSE(recovery.Observe(0, media::VideoStreamOutput{.incomplete_frame = true}, 1510000));
    EXPECT_TRUE(recovery.PollDue(5509999).empty());
    const auto watchdog = recovery.PollDue(5510000);
    ASSERT_EQ(watchdog.size(), 1U);
    EXPECT_EQ(watchdog.front().kind, media::VideoRecoveryRequestKind::kKeyFrame);
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

TEST_F(VideoDeliveryFixture, PartialReferenceFrameHasBoundedRepairDeadlineAndLateFecCompletionCancelsIt) {
    media::VideoRecoveryPolicy recovery{};
    ASSERT_TRUE(Deliver(1, 100, media::VideoFrameKind::kIdr));
    const media::VideoStreamOutput established{.frame = media::VideoFrame{.kind = media::VideoFrameKind::kIdr,
                                                                        .frame_index = 100,
                                                                        .monitor = "test-monitor"}};
    EXPECT_FALSE(recovery.Observe(0, established, 1000000));
    packet_parameters_.frame_index = 2;
    encoded_frame_.frame_index = 101;
    encoded_frame_.kind = media::VideoFrameKind::kPredicted;
    const auto packetized = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
    ASSERT_TRUE(packetized);
    const auto first = media::ParseMedia(packetized->packets.front());
    ASSERT_TRUE(first);
    const auto partial = receiver_.Feed(*first, 1010000);
    ASSERT_TRUE(partial.incomplete_frame);
    EXPECT_FALSE(partial.frame);
    EXPECT_FALSE(recovery.Observe(0, partial, 1010000));
    for (std::size_t packet_index{1}; packet_index < packetized->packets.size(); ++packet_index) {
        const auto packet = media::ParseMedia(packetized->packets[packet_index]);
        ASSERT_TRUE(packet);
        EXPECT_FALSE(recovery.Observe(0, receiver_.Feed(*packet, 1109999), 1109999));
    }
    EXPECT_TRUE(recovery.PollDue(1110000).empty());
    EXPECT_TRUE(recovery.PollDue(9000000).empty());
}

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

TEST_F(VideoDeliveryFixture, MissingTransportFrameRequestsRepairBeforeTheSuccessorIsCompletelyAssembled) {
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
        if (packet_index == 0 || packet_index + 1 == packetized->packets.size()) EXPECT_EQ(received.invalid_reference_frame, 101);
        if (packet_index + 1 < packetized->packets.size()) {
            EXPECT_FALSE(received.completed_frame_index);
        } else {
            EXPECT_EQ(received.completed_frame_index, 102);
            EXPECT_EQ(received.completed_bytes, encoded_frame_.encoded.size());
        }
    }
}

TEST_F(VideoDeliveryFixture, SpeculativeLossStillAllowsLateFecCompletionWithoutReferenceInvalidation) {
    ASSERT_TRUE(Deliver(1, 100, media::VideoFrameKind::kIdr));
    encoded_frame_.frame_index = 101;
    encoded_frame_.kind = media::VideoFrameKind::kPredicted;
    packet_parameters_.frame_index = 2;
    packet_parameters_.minimum_parity = 1;
    const auto packetized = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
    ASSERT_TRUE(packetized);
    ASSERT_EQ(packetized->packets.size(), 4U);
    const std::array<std::size_t, 3> arrival_order{2, 3, 0};
    std::uint64_t arrival_us{1100000};
    for (const auto shard_index : arrival_order) {
        const auto datagram = media::ParseMedia(packetized->packets[shard_index]);
        ASSERT_TRUE(datagram);
        const auto received = receiver_.Feed(*datagram, arrival_us++);
        EXPECT_FALSE(received.invalid_reference_frame);
        EXPECT_FALSE(received.needs_idr);
        if (shard_index == 2) {
            ASSERT_EQ(received.losses.size(), 1U);
            EXPECT_TRUE(received.losses.front().speculative);
        }
        if (shard_index == 0) {
            ASSERT_TRUE(received.frame);
            EXPECT_EQ(received.frame->frame_index, 101U);
            EXPECT_EQ(received.recovered, 1U);
        }
    }
}

TEST_F(VideoDeliveryFixture, CompleteRecoveryCancelsLossFromTheSameFeedIncludingReferenceCounterOverflow) {
    ASSERT_TRUE(Deliver(1, std::numeric_limits<std::uint64_t>::max(), media::VideoFrameKind::kIdr));
    encoded_frame_.frame_index = 0;
    encoded_frame_.kind = media::VideoFrameKind::kIdr;
    encoded_frame_.encoded.assign(100, 1);
    packet_parameters_.frame_index = 3;
    const auto packetized = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
    ASSERT_TRUE(packetized);
    const auto first = media::ParseMedia(packetized->packets.front());
    ASSERT_TRUE(first);
    const auto received = receiver_.Feed(*first, 1100000);
    ASSERT_TRUE(received.frame);
    EXPECT_FALSE(received.invalid_reference_frame);
    EXPECT_FALSE(received.needs_idr);
    ASSERT_EQ(received.losses.size(), 1U);
    EXPECT_FALSE(received.losses.front().speculative);
}

TEST_F(VideoDeliveryFixture, SpeculativeTelemetryDoesNotSuppressRepairWhenTheQueueFinallyRetiresThatFrame) {
    ASSERT_TRUE(Deliver(1, 100, media::VideoFrameKind::kIdr));
    encoded_frame_.frame_index = 101;
    encoded_frame_.kind = media::VideoFrameKind::kPredicted;
    packet_parameters_.frame_index = 2;
    packet_parameters_.minimum_parity = 1;
    const auto partial_frame = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
    ASSERT_TRUE(partial_frame);
    const auto high_shard = media::ParseMedia(partial_frame->packets[2]);
    ASSERT_TRUE(high_shard);
    const auto prediction = receiver_.Feed(*high_shard, 1100000);
    ASSERT_EQ(prediction.losses.size(), 1U);
    EXPECT_TRUE(prediction.losses.front().speculative);
    EXPECT_FALSE(prediction.invalid_reference_frame);
    packet_parameters_.sequence = partial_frame->next_sequence;
    packet_parameters_.frame_index = 3;
    encoded_frame_.frame_index = 102;
    const auto successor = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
    ASSERT_TRUE(successor);
    const auto first = media::ParseMedia(successor->packets.front());
    ASSERT_TRUE(first);
    const auto retired = receiver_.Feed(*first, 1110000);
    ASSERT_FALSE(retired.completed_frame_index);
    EXPECT_EQ(retired.invalid_reference_frame, 101U);
}

TEST_F(VideoDeliveryFixture, FinalLossRequestsRepairOnTheFirstSuccessorShardUsingObservedDirectFrame599) {
    media::VideoRecoveryPolicy recovery{};
    encoded_frame_.monitor = "game-hook";
    encoded_frame_.encoded.assign(19103, 1);
    packet_parameters_.datagram_size = 1100;
    ASSERT_TRUE(Deliver(421, 598, media::VideoFrameKind::kIdr));
    const media::VideoStreamOutput established{
        .frame = media::VideoFrame{.kind = media::VideoFrameKind::kIdr, .frame_index = 598, .monitor = "game-hook"}};
    EXPECT_FALSE(recovery.Observe(0, established, 1000000));

    packet_parameters_.frame_index = 422;
    encoded_frame_.frame_index = 599;
    encoded_frame_.kind = media::VideoFrameKind::kPredicted;
    const auto missing_frame = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
    ASSERT_TRUE(missing_frame);
    ASSERT_EQ(missing_frame->packets.size(), 23U);  // Actual trace: 19 data + 4 parity, only the first 17 arrived.
    for (std::size_t shard_index{}; shard_index < 17; ++shard_index) {
        const auto datagram = media::ParseMedia(missing_frame->packets[shard_index]);
        ASSERT_TRUE(datagram);
        const auto received = receiver_.Feed(*datagram, 1010000 + shard_index);
        EXPECT_FALSE(received.invalid_reference_frame);
        EXPECT_FALSE(recovery.Observe(0, received, 1010000 + shard_index));
    }
    packet_parameters_.sequence = missing_frame->next_sequence;
    packet_parameters_.frame_index = 423;
    encoded_frame_.frame_index = 600;
    const auto successor = media::PacketizeVideoFrame(encoded_frame_, packet_parameters_);
    ASSERT_TRUE(successor);
    const auto first_datagram = media::ParseMedia(successor->packets.front());
    ASSERT_TRUE(first_datagram);
    const auto first_successor = receiver_.Feed(*first_datagram, 1020000);
    ASSERT_FALSE(first_successor.completed_frame_index);
    ASSERT_EQ(first_successor.losses.size(), 1U);
    ASSERT_FALSE(first_successor.losses.front().speculative);
    EXPECT_EQ(first_successor.invalid_reference_frame, 599U);
    const auto request = recovery.Observe(0, first_successor, 1020000);
    ASSERT_TRUE(request);
    EXPECT_EQ(request->kind, media::VideoRecoveryRequestKind::kInvalidateReferences);
    EXPECT_EQ(request->invalid_reference_frame, 599U);
    EXPECT_EQ(request->monitor, "game-hook");
    EXPECT_TRUE(recovery.PollDue(1020010).empty());
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
