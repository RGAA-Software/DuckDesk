#include <gtest/gtest.h>

#include <condition_variable>
#include <future>
#include <thread>

#include "channel.h"
#include "compact_audio_datagram.h"
#include "endpoint_configuration.h"
#include "media_datagrams.h"
#include "paced_media_batch.h"
#include "px_common/async_runtime.h"
#include "video_feedback_datagram.h"
#include "video_frame_rate.h"
#include "video_rate_control.h"

namespace px::transport {
namespace {
TEST(IrohVideoDiagnostics, RecoveryMetadataAndDuplicatesRemainVisibleAfterInitialTraceExpires) {
    VideoReceiveDiagnostics diagnostics{};
    const auto observed_us = diagnostics.Id() + 240'000'000;
    const media::VideoFrame recovery_frame{.kind = media::VideoFrameKind::kReferenceRecovery,
                                           .width = 1280,
                                           .height = 720,
                                           .frame_index = 15699,
                                           .monitor = "game-hook",
                                           .encoded = media::Packet(5000, 7)};
    const auto packets = media::PacketizeVideoFrame(recovery_frame, {.frame_index = 15518, .datagram_size = kMediaDatagramBytes});
    ASSERT_TRUE(packets);
    ASSERT_FALSE(diagnostics.Active(observed_us));
    for (const auto& packet : packets->packets) {
        const auto datagram = media::ParseMedia(packet);
        ASSERT_TRUE(datagram);
        diagnostics.Observe(*datagram, observed_us);
    }
    diagnostics.Observe(*media::ParseMedia(packets->packets.front()), observed_us + 10);
    const auto incomplete = diagnostics.Inspect(0, 15518);
    ASSERT_TRUE(incomplete);
    EXPECT_EQ(incomplete->encoder_frame, 15699);
    EXPECT_EQ(incomplete->kind, media::VideoFrameKind::kReferenceRecovery);
    EXPECT_EQ(incomplete->unique_shards, packets->packets.size());
    EXPECT_EQ(incomplete->duplicate_shards, 1);
    EXPECT_EQ(incomplete->first_packet_received_us, observed_us);
    EXPECT_EQ(incomplete->latest_packet_received_us, observed_us + 10);
    EXPECT_FALSE(incomplete->completed);
    diagnostics.Completed(0, 15518, 15699, media::VideoFrameKind::kReferenceRecovery, true);
    const auto completed = diagnostics.Inspect(0, 15518);
    ASSERT_TRUE(completed);
    EXPECT_TRUE(completed->completed);
    EXPECT_TRUE(completed->delivered);
    diagnostics.Flush(observed_us + 600'000);
    EXPECT_FALSE(diagnostics.Inspect(0, 15518));
}

TEST(IrohVideoDiagnostics, MissingDescriptorDoesNotInventEncoderIdentityAndFecCompletionCanSupplyIt) {
    VideoReceiveDiagnostics diagnostics{};
    const auto observed_us = diagnostics.Id() + 240'000'000;
    const media::VideoFrame recovery_frame{
        .kind = media::VideoFrameKind::kReferenceRecovery, .width = 1280, .height = 720, .frame_index = 15705, .encoded = media::Packet(5000, 7)};
    const auto packets = media::PacketizeVideoFrame(recovery_frame, {.frame_index = 15524, .datagram_size = kMediaDatagramBytes});
    ASSERT_TRUE(packets);
    for (std::size_t packet_index{1}; packet_index < packets->packets.size(); ++packet_index) {
        diagnostics.Observe(*media::ParseMedia(packets->packets[packet_index]), observed_us);
    }
    const auto incomplete = diagnostics.Inspect(0, 15524);
    ASSERT_TRUE(incomplete);
    EXPECT_EQ(incomplete->encoder_frame, 0);
    EXPECT_FALSE(incomplete->kind);
    EXPECT_EQ(incomplete->unique_shards, packets->packets.size() - 1);
    diagnostics.Completed(0, 15524, 15705, media::VideoFrameKind::kReferenceRecovery, true);
    const auto completed = diagnostics.Inspect(0, 15524);
    ASSERT_TRUE(completed);
    EXPECT_EQ(completed->encoder_frame, 15705);
    EXPECT_EQ(completed->kind, media::VideoFrameKind::kReferenceRecovery);
    EXPECT_TRUE(completed->delivered);
}

TEST(IrohVideoDiagnostics, IncompleteHistoryRemainsBoundedAndRepeatedShutdownFlushIsSafe) {
    VideoReceiveDiagnostics diagnostics{};
    const auto observed_us = diagnostics.Id() + 240'000'000;
    const media::VideoFrame predicted_frame{.width = 1280, .height = 720, .frame_index = 1, .encoded = media::Packet(5000, 7)};
    for (std::uint32_t wire_frame{1}; wire_frame <= 65; ++wire_frame) {
        const auto packets = media::PacketizeVideoFrame(predicted_frame, {.frame_index = wire_frame, .datagram_size = kMediaDatagramBytes});
        ASSERT_TRUE(packets);
        diagnostics.Observe(*media::ParseMedia(packets->packets.front()), observed_us);
    }
    EXPECT_FALSE(diagnostics.Inspect(0, 1));
    EXPECT_TRUE(diagnostics.Inspect(0, 65));
    diagnostics.Flush(observed_us, true);
    diagnostics.Flush(observed_us, true);
    EXPECT_FALSE(diagnostics.Inspect(0, 65));
}

TEST(IrohVideoQueueAdmission, ConfiguredBudgetBoundsUnsentBytesWithoutUsingRttOrFeedbackAge) {
    const VideoQueueAdmission normal{};
    EXPECT_EQ(normal.BudgetBytes(), 19200);
    EXPECT_TRUE(normal.CanSubmit(65536));
    EXPECT_TRUE(normal.CanSubmit(65536 - 14800));
    EXPECT_FALSE(normal.CanSubmit(65536 - 14801));
    EXPECT_FALSE(normal.CanSubmit(0));
    EXPECT_FALSE(normal.CanSubmit(65536, 19201));
    EXPECT_EQ(VideoQueueAdmission{VideoSendBudget{128000}}.BudgetBytes(), 4400);
    EXPECT_EQ(VideoQueueAdmission{VideoSendBudget{200000000}}.BudgetBytes(), 32768);
    // Return-path feedback can be old while QUIC has no unsent backlog.
    EXPECT_TRUE(normal.CanSubmit(VideoQueueAdmission::kTransportBufferBytes));
}

TEST(IrohVideoQueueAdmission, AckStallBoundsQueueYieldsToControlAndPreservesEveryAcceptedFragment) {
    asio::io_context executor{};
    const VideoQueueAdmission admission{};
    struct QueueState final {
        std::uint64_t queued_bytes{};
        std::uint64_t peak_bytes{};
        std::size_t blocked_checks{};
        std::size_t completions{};
        bool recovered{};
        bool control_ran{};
        std::vector<std::uint8_t> submitted{};
    };
    const auto state = std::make_shared<QueueState>();
    std::vector<media::Packet> packets{};
    for (std::uint8_t packet_index{}; packet_index < 80; ++packet_index) packets.emplace_back(1100, packet_index);
    const auto batch = std::make_shared<PacedMediaBatch>(
        executor.get_executor(), std::move(packets), true,
        [state](std::span<const media::Packet> burst) {
            for (const auto& packet : burst) {
                state->queued_bytes += packet.size();
                state->submitted.push_back(packet.front());
            }
            state->peak_bytes = std::max(state->peak_bytes, state->queued_bytes);
            if (state->recovered) state->queued_bytes = 0;
            return true;
        },
        [] { return true; },
        [state](bool succeeded) {
            EXPECT_TRUE(succeeded);
            ++state->completions;
        },
        std::make_shared<VideoSendPacing>(),
        [state, admission](std::span<const media::Packet> burst) {
            std::size_t burst_bytes{};
            for (const auto& packet : burst) burst_bytes += packet.size();
            const bool ready = admission.CanSubmit(VideoQueueAdmission::kTransportBufferBytes - state->queued_bytes, burst_bytes);
            if (!ready) ++state->blocked_checks;
            return ready;
        });
    const auto recovery_timer = std::make_shared<asio::steady_timer>(executor, std::chrono::milliseconds(160));
    recovery_timer->async_wait([state, recovery_timer](const asio::error_code& error) {
        ASSERT_FALSE(error);
        EXPECT_GT(state->blocked_checks, 0);
        EXPECT_TRUE(state->control_ran);
        state->queued_bytes = 0;
        state->recovered = true;
    });
    batch->Start();
    asio::post(executor, [state] { state->control_ran = true; });
    executor.run();
    EXPECT_EQ(state->completions, 1);
    EXPECT_LE(state->peak_bytes, admission.BudgetBytes());
    ASSERT_EQ(state->submitted.size(), 80);
    for (std::size_t packet_index{}; packet_index < state->submitted.size(); ++packet_index) EXPECT_EQ(state->submitted[packet_index], packet_index);
}

TEST(IrohVideoQueueAdmission, ExpiryOrStopDuringQueueWaitCompletesOnceAndReleasesTheBatch) {
    for (const bool stop_from_callback : {false, true}) {
        asio::io_context executor{};
        struct WaitState final {
            bool stopped{};
            std::size_t completed{};
            std::size_t sent{};
        };
        const auto state = std::make_shared<WaitState>();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15);
        auto batch = std::make_shared<PacedMediaBatch>(
            executor.get_executor(), std::vector<media::Packet>(1, media::Packet(1100)), true,
            [state](std::span<const media::Packet>) {
                ++state->sent;
                return true;
            },
            [state, deadline] { return !state->stopped && std::chrono::steady_clock::now() < deadline; },
            [state](bool succeeded) {
                EXPECT_FALSE(succeeded);
                state->stopped = true;
                ++state->completed;
            },
            std::make_shared<VideoSendPacing>(), [](std::span<const media::Packet>) { return false; });
        const auto observer = std::weak_ptr<PacedMediaBatch>{batch};
        batch->Start();
        batch.reset();
        if (stop_from_callback) asio::post(executor, [state] { state->stopped = true; });
        executor.run();
        EXPECT_TRUE(observer.expired());
        EXPECT_EQ(state->completed, 1);
        EXPECT_EQ(state->sent, 0);
    }
}

TEST(IrohVideoSendPacing, ApplicationBudgetsScaleEncoderAndPacingIndependently) {
    const auto started = VideoSendPacing::Clock::time_point{} + std::chrono::seconds(1);
    for (const auto total_bps : {8'001'000ULL, 10'000'000ULL, 20'000'000ULL}) {
        const VideoSendBudget budget{total_bps};
        EXPECT_EQ(budget.TotalBitsPerSecond(), total_bps);
        EXPECT_EQ(budget.EncoderBitsPerSecond(), total_bps * 65 / 100);
        VideoSendPacing pacing{budget};
        pacing.Sent(4800, started);
        pacing.Sent(4800, started);
        const auto expected_ns = (4800ULL * 8'000'000'000ULL + budget.VideoWireBitsPerSecond() - 1) / budget.VideoWireBitsPerSecond();
        EXPECT_EQ(pacing.NextSendTime(), started + std::chrono::nanoseconds{expected_ns});
    }
    EXPECT_GT(VideoSendBudget{20'000'000}.EncoderBitsPerSecond(), 6'500'000);
}

TEST(IrohVideoSendPacing, KeepsCreditAcrossFramesAndBoundsLateExecutorCatchup) {
    VideoSendPacing pacing{};
    const auto started = VideoSendPacing::Clock::time_point{} + std::chrono::seconds(1);
    pacing.Sent(4800, started);
    pacing.Sent(4800, started);
    EXPECT_EQ(pacing.NextSendTime(), started + std::chrono::milliseconds(4));
    const auto after_idle = started + std::chrono::seconds(5);
    pacing.Sent(4800, after_idle);
    pacing.Sent(4800, after_idle);
    EXPECT_EQ(pacing.NextSendTime(), after_idle + std::chrono::milliseconds(4));
}

TEST(IrohVideoSendPacing, SmallFramesShareBudgetAndControlRunsWhileWaiting) {
    asio::io_context executor{};
    const auto pacing = std::make_shared<VideoSendPacing>();
    struct State final {
        std::size_t sent{};
        std::size_t completed{};
        bool control_ran{};
        bool video_after_control{};
    };
    const auto state = std::make_shared<State>();
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t frame_index{}; frame_index < 20; ++frame_index) {
        const auto batch = std::make_shared<PacedMediaBatch>(
            executor.get_executor(), std::vector<media::Packet>(1, media::Packet(1100)), true,
            [state](std::span<const media::Packet> packets) {
                state->sent += packets.size();
                state->video_after_control = state->video_after_control || state->control_ran;
                return true;
            },
            [] { return true; },
            [state](bool succeeded) {
                EXPECT_TRUE(succeeded);
                ++state->completed;
            },
            pacing);
        asio::post(executor, [batch] { batch->Start(); });
    }
    asio::post(executor, [state] { state->control_ran = true; });
    executor.run();
    EXPECT_EQ(state->sent, 20);
    EXPECT_EQ(state->completed, 20);
    EXPECT_TRUE(state->video_after_control);
    EXPECT_GE(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(18));
}

TEST(IrohVideoSendPacing, CancelledWaitingFrameCompletesOnceAndReleasesOwner) {
    asio::io_context executor{};
    const auto pacing = std::make_shared<VideoSendPacing>();
    struct State final {
        bool cancelled{};
        std::size_t sent{};
        std::size_t completed{};
        std::size_t admission_checks{};
    };
    const auto state = std::make_shared<State>();
    auto batch = std::make_shared<PacedMediaBatch>(
        executor.get_executor(), std::vector<media::Packet>(1, media::Packet(1100)), true,
        [state](std::span<const media::Packet>) {
            ++state->sent;
            return true;
        },
        [state] { return !state->cancelled; },
        [state](bool succeeded) {
            EXPECT_FALSE(succeeded);
            ++state->completed;
        },
        pacing, [state](std::span<const media::Packet>) {
            ++state->admission_checks;
            return false;
        });
    const auto observer = std::weak_ptr<PacedMediaBatch>{batch};
    batch->Start();
    batch.reset();
    asio::post(executor, [state] { state->cancelled = true; });
    executor.run();
    EXPECT_EQ(state->sent, 0);
    EXPECT_EQ(state->completed, 1);
    EXPECT_GE(state->admission_checks, 1);
    EXPECT_TRUE(observer.expired());
}

TEST(IrohVideoDiagnostics, PacketIdentityKeepsWireFrameSeparateFromEncoderFrameAndHandlesSequenceWrap) {
    const media::VideoFrame frame{
        .kind = media::VideoFrameKind::kIdr, .width = 1280, .height = 720, .frame_index = 9001, .encoded = media::Packet(6000, 0x41)};
    const auto packets = media::PacketizeVideoFrame(
        frame, {.frame_index = 31, .timestamp_90khz = 123456, .sequence = 65534, .datagram_size = kMediaDatagramBytes, .fec_percent = 20});
    ASSERT_TRUE(packets);
    ASSERT_GT(packets->packets.size(), 3);
    for (std::size_t packet_index{}; packet_index < packets->packets.size(); ++packet_index) {
        const auto envelope = media::ParseMedia(packets->packets[packet_index]);
        ASSERT_TRUE(envelope);
        const auto identity = ReadVideoPacketIdentity(envelope->payload);
        ASSERT_TRUE(identity);
        EXPECT_EQ(identity->wire_frame, 31);
        EXPECT_EQ(identity->timestamp, 123456);
        EXPECT_EQ(identity->sequence, static_cast<std::uint16_t>(65534 + packet_index));
        EXPECT_LT(identity->shard, identity->data_count + identity->parity_count);
    }
    EXPECT_FALSE(ReadVideoPacketIdentity({}));
    EXPECT_FALSE(ReadVideoPacketIdentity(media::Packet(32, 0)));
}

TEST(IrohVideoTransitTiming, SourcePausesAndClockOffsetsDoNotBecomeNetworkDelay) {
    VideoTransitTiming timing{};
    static_cast<void>(timing.Observe(90000, 50000000));
    const auto stable = timing.Observe(91500, 50016667);
    EXPECT_LT(stable.excess_transit_us, 2);
    const auto paused_source = timing.Observe(114000, 50266667);
    EXPECT_EQ(paused_source.source_gap_us, 250000);
    EXPECT_LT(paused_source.excess_transit_us, 2);
    const auto delayed = timing.Observe(115500, 50433334);
    EXPECT_NEAR(delayed.excess_transit_us, 150000, 2);
    const auto recovered = timing.Observe(132000, 50466667);
    EXPECT_LT(recovered.excess_transit_us, 2);
}

TEST(IrohVideoTransitTiming, RtpWrapDoesNotCreateAFalseDelay) {
    VideoTransitTiming timing{};
    constexpr std::uint32_t start_timestamp{0xfffffc00U};
    static_cast<void>(timing.Observe(start_timestamp, 1000000));
    const auto wrapped = timing.Observe(start_timestamp + 1500U, 1016667);
    EXPECT_EQ(wrapped.source_gap_us, 16666);
    EXPECT_LT(wrapped.excess_transit_us, 2);
}

TEST(IrohVideoRateControl, SustainedRelayQueueGrowthReducesEncoderRateAndHealthyPathRecoversInSpacedSteps) {
    VideoRateControl control{};
    const auto start = VideoRateControl::Clock::time_point{};
    ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 6000};
    EXPECT_EQ(control.Update(14'000'000, path, start), 14'000'000);
    path.rtt_us = 860000;  // Reproduced Relay stall: zero QUIC loss, large RTT growth.
    EXPECT_EQ(control.Update(14'000'000, path, start + std::chrono::milliseconds(500)), 14'000'000);
    EXPECT_EQ(control.Update(14'000'000, path, start + std::chrono::milliseconds(501)), 14'000'000);
    EXPECT_EQ(control.Update(14'000'000, path, start + std::chrono::seconds(1)), 7'000'000);
    EXPECT_EQ(control.Update(14'000'000, path, start + std::chrono::milliseconds(1500)), 3'500'000);
    path.rtt_us = 7000;
    EXPECT_EQ(control.Update(14'000'000, path, start + std::chrono::seconds(2)), 3'500'000);
    EXPECT_EQ(control.Update(14'000'000, path, start + std::chrono::milliseconds(3500)), 3'500'000);
    const auto recovered = control.Update(14'000'000, path, start + std::chrono::milliseconds(6500));
    EXPECT_GT(recovered, 3'500'000);
    EXPECT_LT(recovered, 4'500'000);
    EXPECT_EQ(control.Update(14'000'000, path, start + std::chrono::seconds(7)), recovered);
    EXPECT_EQ(control.Update(2'000'000, path, start + std::chrono::milliseconds(7100)), 2'000'000);
}

TEST(IrohVideoRateControl, StableHighLatencyAndPathChangesDoNotMeanQueueGrowth) {
    VideoRateControl control{};
    const auto start = VideoRateControl::Clock::time_point{};
    ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 180000};
    EXPECT_EQ(control.Update(8'000'000, path, start), 8'000'000);
    path.rtt_us = 190000;
    EXPECT_EQ(control.Update(8'000'000, path, start + std::chrono::seconds(1)), 8'000'000);
    path = {.path = PathKind::kDirect, .rtt_us = 8000};
    EXPECT_EQ(control.Update(8'000'000, path, start + std::chrono::seconds(2)), 8'000'000);
    path.rtt_us = 800000;
    for (int sample_index{3}; sample_index < 12; ++sample_index)
        EXPECT_GE(control.Update(8'000'000, path, start + std::chrono::seconds(sample_index)), 250'000);
}

TEST(IrohVideoRateControl, RelayQueueRecoveryWaitsBeforeSmallProbeAndNewPressureRestartsTheWait) {
    VideoRateControl control{};
    const auto started = VideoRateControl::Clock::time_point{};
    ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 6000};
    EXPECT_EQ(control.Update(13'000'000, path, started), 13'000'000);
    path.rtt_us = 340000;
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::milliseconds(500), true), 13'000'000);
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::seconds(1), true), 6'500'000);
    path.rtt_us = 6000;
    // Recorded Relay batches returned to low RTT briefly, probed by 25%,
    // and exhausted capture credit again. Queue pressure needs the same
    // recovery hold as packet loss, without requiring a QUIC loss counter.
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::seconds(3)), 6'500'000);
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::seconds(4), true), 6'500'000);
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::seconds(6)), 6'500'000);
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::seconds(9)), 7'150'000);
}

TEST(IrohVideoRateControl, HealthyCeilingChangeKeepsNormalRecoveryWithoutInventingQueuePressure) {
    VideoRateControl control{};
    const auto started = VideoRateControl::Clock::time_point{};
    const ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 180000};
    EXPECT_EQ(control.Update(4'000'000, path, started), 4'000'000);
    EXPECT_EQ(control.Update(8'000'000, path, started + std::chrono::seconds(2)), 5'000'000);
}

TEST(IrohVideoRateControl, RecordedDirectLossReducesRateBeforeCaptureBackpressure) {
    VideoRateControl control{};
    const auto started = VideoRateControl::Clock::time_point{};
    ConnectionSnapshot path{.path = PathKind::kDirect, .rtt_us = 11083, .sent_packets = 74, .lost_packets = 0};
    EXPECT_EQ(control.Update(13'000'000, path, started), 13'000'000);
    // Actual successive snapshots from the failed 20Mbps direct connection.
    // RTT recovers below the queue threshold while loss and reference waits continue.
    path.rtt_us = 92586;
    path.sent_packets = 564;
    path.lost_packets = 100;
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::microseconds(514290)), 13'000'000);
    path.rtt_us = 83070;
    path.sent_packets = 856;
    path.lost_packets = 109;
    EXPECT_LT(control.Update(13'000'000, path, started + std::chrono::microseconds(1057183)), 13'000'000);
}

TEST(IrohVideoRateControl, IsolatedLossSparseSceneAndHistoricalCountersDoNotReduceQuality) {
    VideoRateControl control{};
    const auto started = VideoRateControl::Clock::time_point{};
    ConnectionSnapshot path{.path = PathKind::kDirect, .rtt_us = 6000, .sent_packets = 10000, .lost_packets = 3000};
    EXPECT_EQ(control.Update(13'000'000, path, started), 13'000'000);
    path.sent_packets += 500;
    path.lost_packets += 20;
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::milliseconds(500)), 13'000'000);
    path.sent_packets += 500;
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::seconds(1)), 13'000'000);
    path.sent_packets += 30;
    path.lost_packets += 20;
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::milliseconds(1500)), 13'000'000);
    path.sent_packets += 500;
    path.lost_packets += 1;
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::seconds(2)), 13'000'000);
    path.sent_packets = 200;
    path.lost_packets = 50;
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::milliseconds(2500)), 13'000'000);
    EXPECT_FALSE(control.LossPressure());
    path.path = PathKind::kRelay;
    path.sent_packets += 500;
    path.lost_packets += 50;
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::seconds(3)), 13'000'000);
}

TEST(IrohVideoRateControl, SustainedDirectLossHoldsRecoveryBeforeSmallQualityProbe) {
    VideoRateControl control{};
    const auto started = VideoRateControl::Clock::time_point{};
    ConnectionSnapshot path{.path = PathKind::kDirect, .rtt_us = 6000};
    EXPECT_EQ(control.Update(13'000'000, path, started), 13'000'000);
    for (int sample_index{1}; sample_index <= 2; ++sample_index) {
        path.sent_packets += 500;
        path.lost_packets += 20;
        static_cast<void>(control.Update(13'000'000, path, started + std::chrono::milliseconds(sample_index * 500)));
    }
    const auto reduced_rate = control.Update(13'000'000, path, started + std::chrono::milliseconds(1001));
    ASSERT_EQ(reduced_rate, 10'400'000);
    for (int sample_index{3}; sample_index <= 10; ++sample_index) {
        path.sent_packets += 500;
        EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::milliseconds(sample_index * 500)), reduced_rate);
    }
    path.sent_packets += 500;
    EXPECT_EQ(control.Update(13'000'000, path, started + std::chrono::milliseconds(6000)), 11'440'000);
    EXPECT_FALSE(control.LossPressure());
}

TEST(IrohVideoRateControl, PersistentRelayCongestionCanReduceBelowOneMegabitWithoutExceedingTheCeiling) {
    VideoRateControl control{};
    const auto start = VideoRateControl::Clock::time_point{};
    ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 5000};
    EXPECT_EQ(control.Update(4'000'000, path, start), 4'000'000);
    path.rtt_us = 500000;
    std::uint64_t reduced_rate{};
    for (int sample_index{1}; sample_index <= 12; ++sample_index)
        reduced_rate = control.Update(4'000'000, path, start + std::chrono::milliseconds(500 * sample_index));
    EXPECT_EQ(reduced_rate, 250'000);
    EXPECT_EQ(control.Update(150'000, path, start + std::chrono::seconds(7)), 150'000);
}

TEST(IrohVideoRateControl, LowRttDoesNotHideMediaQueueBackpressure) {
    VideoRateControl control{};
    const auto start = VideoRateControl::Clock::time_point{};
    const ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 6000};
    EXPECT_EQ(control.Update(8'000'000, path, start), 8'000'000);
    std::uint64_t reduced_rate{};
    for (int sample_index{1}; sample_index <= 40; ++sample_index)
        reduced_rate = control.Update(8'000'000, path, start + std::chrono::milliseconds(500 * sample_index), true);
    EXPECT_EQ(reduced_rate, 250'000);
    EXPECT_EQ(control.Update(8'000'000, path, start + std::chrono::milliseconds(20500)), reduced_rate);
    EXPECT_EQ(control.Update(8'000'000, path, start + std::chrono::seconds(22)), reduced_rate);
    EXPECT_GT(control.Update(8'000'000, path, start + std::chrono::seconds(25)), reduced_rate);
}

TEST(IrohVideoRateControl, IntermittentRttAndCaptureCreditSpikesDoNotDestroyQuality) {
    VideoRateControl control{};
    const auto start = VideoRateControl::Clock::time_point{};
    ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 6000};
    constexpr std::uint64_t ceiling{14000000};
    EXPECT_EQ(control.Update(ceiling, path, start), ceiling);
    for (int sample_index{1}; sample_index <= 20; ++sample_index) {
        const bool transient_pressure = sample_index % 3 != 0;
        path.rtt_us = transient_pressure ? 120000 : 6000;
        EXPECT_EQ(control.Update(ceiling, path, start + std::chrono::milliseconds(500 * sample_index), transient_pressure), ceiling);
    }
    // Actual missing receipt bypasses the transient-pressure delay.
    EXPECT_LT(control.Update(ceiling, path, start + std::chrono::seconds(11), false, true, 1000000), 1000000);
}

TEST(IrohVideoRateControl, PersistentMildPressureReducesGraduallyAndZeroRttDoesNotPoisonTheBaseline) {
    VideoRateControl control{};
    const auto start = VideoRateControl::Clock::time_point{};
    ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 0};
    EXPECT_EQ(control.Update(8000000, path, start, true), 8000000);
    path.rtt_us = 180000;
    EXPECT_EQ(control.Update(8000000, path, start + std::chrono::seconds(1)), 8000000);
    std::uint64_t reduced_rate{};
    for (int sample_index{3}; sample_index <= 8; ++sample_index)
        reduced_rate = control.Update(8000000, path, start + std::chrono::milliseconds(500 * sample_index), true);
    EXPECT_LT(reduced_rate, 8000000);
    EXPECT_GT(reduced_rate, 4000000);
}

TEST(IrohVideoFeedback, ShortEmptyReportsReleaseCreditWithoutCollapsingTheBitrateEstimate) {
    VideoDeliveryProgress progress{};
    const auto start = VideoDeliveryProgress::Clock::time_point{};
    progress.Offered(0, 1, start);
    ASSERT_TRUE(progress.Observe({.elapsed_us = 500000, .complete_frames = 1, .complete_bytes = 62500, .latest_frame_index = 1}, start));
    EXPECT_EQ(progress.ReceivedBitrate(), 1000000);
    ASSERT_TRUE(progress.Observe({.elapsed_us = 550000, .complete_frames = 1, .complete_bytes = 62500, .latest_frame_index = 1}, start));
    EXPECT_EQ(progress.ReceivedBitrate(), 1000000);
    EXPECT_FALSE(progress.Congested(start + std::chrono::seconds(1)));
    ASSERT_TRUE(progress.Observe({.elapsed_us = 1000000, .complete_frames = 1, .complete_bytes = 62500, .latest_frame_index = 1}, start));
    EXPECT_EQ(progress.ReceivedBitrate(), 0);
}

TEST(IrohVideoFeedback, IncompleteAssemblyReportsStallAndUndecodableCompleteFramesCountAsProgress) {
    VideoReceiveProgress progress{};
    progress.Observe(2, {}, 100);
    EXPECT_TRUE(progress.Poll(50000).empty());
    auto reports = progress.Poll(50100);
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports.front().complete_frames, 0);
    EXPECT_FALSE(reports.front().latest_frame_index);
    progress.Observe(2, media::VideoStreamOutput{.invalid_reference_frame = 10, .completed_frame_index = 12, .completed_bytes = 10000}, 300000);
    reports = progress.Poll(500100);
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports.front().complete_frames, 1);
    EXPECT_EQ(reports.front().complete_bytes, 10000);
    EXPECT_EQ(reports.front().latest_frame_index, 12);
    EXPECT_EQ(progress.Poll(750100).front().complete_frames, 1);
}

TEST(IrohVideoFeedback, MissingFeedbackReducesRateButAcknowledgedStaticVideoDoesNot) {
    VideoDeliveryProgress progress{};
    VideoRateControl control{};
    const auto start = VideoRateControl::Clock::time_point{};
    const ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 6000};
    progress.Offered(0, 100, start);
    EXPECT_EQ(control.Update(8000000, path, start), 8000000);
    EXPECT_TRUE(progress.Congested(start + std::chrono::seconds(1)));
    EXPECT_EQ(control.Update(8000000, path, start + std::chrono::seconds(1), false, progress.Congested(start + std::chrono::seconds(1))), 4000000);
    EXPECT_TRUE(progress.Observe({.stream = 0, .elapsed_us = 1000000, .complete_frames = 1, .complete_bytes = 10000, .latest_frame_index = 100},
                                 start + std::chrono::seconds(1)));
    EXPECT_FALSE(progress.Congested(start + std::chrono::seconds(30)));
    EXPECT_FALSE(progress.Observe({.stream = 0, .elapsed_us = 250000}, start + std::chrono::seconds(2)));
}

TEST(IrohVideoFeedback, DroppedFeedbackAndFrameHistoryRemainBoundedWithoutHidingLongStalls) {
    VideoDeliveryProgress progress{};
    const auto start = VideoDeliveryProgress::Clock::time_point{};
    for (std::uint64_t frame_index{1}; frame_index <= 1000; ++frame_index)
        progress.Offered(0, frame_index, start + std::chrono::milliseconds(frame_index));
    EXPECT_TRUE(progress.Congested(start + std::chrono::seconds(2)));
    EXPECT_TRUE(progress.Observe({.elapsed_us = 2000000, .complete_frames = 10, .complete_bytes = 100000, .latest_frame_index = 1000},
                                 start + std::chrono::seconds(2)));
    EXPECT_FALSE(progress.Congested(start + std::chrono::seconds(30)));
    EXPECT_EQ(progress.ReceivedBitrate(), 400000);
    EXPECT_FALSE(progress.Observe({.elapsed_us = 2500000, .complete_frames = 9, .complete_bytes = 100000}, start + std::chrono::seconds(3)));
    progress.Offered(1, 1, start + std::chrono::seconds(3));
    EXPECT_TRUE(progress.Congested(start + std::chrono::seconds(4)));
}

TEST(IrohVideoRateControl, ActualReceptionCapsCongestedEncoderEvenBeforeRttCatchesUp) {
    VideoRateControl control{};
    const auto start = VideoRateControl::Clock::time_point{};
    const ConnectionSnapshot path{.path = PathKind::kRelay, .rtt_us = 6000};
    EXPECT_EQ(control.Update(8000000, path, start), 8000000);
    EXPECT_EQ(control.Update(8000000, path, start + std::chrono::seconds(1), false, true, 1000000), 800000);
    EXPECT_EQ(control.Update(8000000, path, start + std::chrono::seconds(2), false, true, 0), 400000);
}

TEST(IrohAudioDatagram, RemovesOnlyPaddingAndRestoresEveryDataAndParityByte) {
    media::AudioPacketizer packetizer{};
    std::size_t padded_bytes{};
    std::size_t compact_bytes{};
    std::size_t packets_seen{};
    for (std::size_t packet_index{}; packet_index < 8; ++packet_index) {
        media::Packet opus(100 + packet_index, 0x37);
        opus.back() = 0;  // A real trailing zero in Opus is restored, not lost.
        for (const auto& packet : packetizer.Push(opus, kMediaDatagramBytes)) {
            const auto compact = CompactAudioDatagram(packet);
            ASSERT_FALSE(compact.empty());
            const auto expanded = ExpandAudioDatagram(compact);
            ASSERT_TRUE(expanded);
            EXPECT_EQ(*expanded, packet);
            compact_bytes += compact.size();
            padded_bytes += packet.size();
            ++packets_seen;
        }
    }
    EXPECT_EQ(packets_seen, 12);
    EXPECT_LT(compact_bytes * 5, padded_bytes);
    EXPECT_FALSE(ExpandAudioDatagram(media::Packet{'P', 'X', 'A', 1, 0xFF, 0xFF, 0, 0}));
    EXPECT_FALSE(ExpandAudioDatagram(media::Packet{'P', 'X', 'A', 1, 0, 1, 0, 0}));
}

TEST(IrohVideoFlight, DelayedFeedbackWithSmallFramesDoesNotPauseBeforeCreditBounds) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    for (std::uint64_t frame_index{1}; frame_index <= 8; ++frame_index) {
        flight.Sent(0, frame_index, 3300, started + std::chrono::milliseconds((frame_index - 1) * 16));
    }
    EXPECT_EQ(flight.PendingBytes(), 26400);
    EXPECT_TRUE(flight.CanSend(0, 3300, started + std::chrono::milliseconds(133)));
    EXPECT_TRUE(flight.CanSend(0, 3300, started + std::chrono::milliseconds(220)));
    flight.Observe({.elapsed_us = 250000, .complete_frames = 3, .latest_frame_index = 3}, started + std::chrono::milliseconds(250));
    EXPECT_TRUE(flight.CanSend(0, 3300, started + std::chrono::milliseconds(250)));
}

TEST(IrohVideoFlight, LocalCompletionDoesNotReleaseRemoteCreditAndMissingFramesCanProbe) {
    VideoFlightWindow flight{};
    const auto start = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    for (std::uint64_t frame_index{1}; frame_index <= 8; ++frame_index) {
        EXPECT_TRUE(flight.CanSend(0, 0, start));
        flight.Sent(0, frame_index, 32768, start);
    }
    EXPECT_FALSE(flight.CanSend(0, 10000, start + std::chrono::milliseconds(100)));
    EXPECT_EQ(flight.PendingBytes(), 262144);
    EXPECT_TRUE(flight.CanSend(0, 10000, start + std::chrono::milliseconds(250)));
    flight.Sent(0, 9, 10000, start + std::chrono::milliseconds(250));
    EXPECT_FALSE(flight.CanSend(0, 10000, start + std::chrono::milliseconds(251)));
    flight.Observe({.elapsed_us = 300000, .complete_frames = 1, .latest_frame_index = 9}, start + std::chrono::milliseconds(300));
    EXPECT_EQ(flight.PendingBytes(), 0);
    EXPECT_TRUE(flight.CanSend(0, 10000, start + std::chrono::milliseconds(301)));
}

TEST(IrohVideoFlight, ByteBoundsPreserveStaticFramesAndRejectStaleFeedback) {
    VideoFlightWindow flight{};
    const auto start = VideoFlightWindow::Clock::time_point{};
    flight.Sent(0, 1, 200000, start);
    EXPECT_FALSE(flight.CanSend(0, 100000, start));
    EXPECT_TRUE(flight.CanSend(0, 1000, start + std::chrono::milliseconds(210)));
    flight.Observe({.elapsed_us = 300000, .complete_frames = 1, .latest_frame_index = 1}, start + std::chrono::milliseconds(300));
    EXPECT_TRUE(flight.CanSend(0, 100000, start + std::chrono::seconds(30)));
    flight.Sent(0, 2, 200000, start + std::chrono::seconds(30));
    flight.Observe({.elapsed_us = 200000, .complete_frames = 1, .latest_frame_index = 2}, start + std::chrono::seconds(30));
    EXPECT_EQ(flight.PendingBytes(), 200000);
    EXPECT_TRUE(flight.CanSend(1, 100000, start + std::chrono::seconds(30)));
    VideoFlightWindow distant{};
    distant.ObserveRtt(200000);
    for (std::uint64_t frame_index{1}; frame_index <= 16; ++frame_index) {
        EXPECT_TRUE(distant.CanSend(0, 1000, start + std::chrono::milliseconds(250)));
        distant.Sent(0, frame_index, 1000, start);
    }
}

TEST(IrohVideoFeedbackDatagram, PreservesFrameZeroAndRejectsMalformedPackets) {
    const VideoReceiveFeedback report{.stream = 3, .elapsed_us = 50000, .complete_frames = 1, .complete_bytes = 150000, .latest_frame_index = 0};
    auto packet = EncodeVideoFeedbackDatagram(report);
    const auto decoded = DecodeVideoFeedbackDatagram(packet);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->stream, 3);
    EXPECT_EQ(decoded->elapsed_us, 50000);
    EXPECT_EQ(decoded->complete_bytes, 150000);
    ASSERT_TRUE(decoded->latest_frame_index);
    EXPECT_EQ(*decoded->latest_frame_index, 0);
    packet[5] = 2;
    EXPECT_FALSE(DecodeVideoFeedbackDatagram(packet));
    packet[5] = 1;
    packet.pop_back();
    EXPECT_FALSE(DecodeVideoFeedbackDatagram(packet));
    EXPECT_FALSE(DecodeVideoFeedbackDatagram(media::Packet{}));
    EXPECT_FALSE(DecodeVideoFeedbackDatagram(EncodeVoiceDatagram(Message{})));
}

TEST(IrohVideoFlight, FreshCompleteProgressReleasesCaptureBeforeSparseProbeDeadline) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    for (std::uint64_t frame_index{1}; frame_index <= 8; ++frame_index) {
        flight.Sent(0, frame_index, 32768, started + std::chrono::milliseconds((frame_index - 1) * 16));
    }
    const auto progress_time = started + std::chrono::milliseconds(240);
    ASSERT_FALSE(flight.CanSend(0, 0, progress_time));
    // Matches the live trace: 8 -> 7 outstanding frames, capacity is free,
    // but the remaining oldest frame is 224ms old and the probe is not due.
    flight.Observe({.elapsed_us = 240000, .complete_frames = 1, .complete_bytes = 32000, .latest_frame_index = 1}, progress_time);
    EXPECT_EQ(flight.PendingBytes(), 229376);
    EXPECT_TRUE(flight.CanSend(0, 0, progress_time));
    flight.Observe({.elapsed_us = 260000, .complete_frames = 6, .complete_bytes = 192000, .latest_frame_index = 6},
                   started + std::chrono::milliseconds(260));
    flight.Sent(0, 9, 32000, started + std::chrono::milliseconds(261));
    // Repeating the same complete frame cannot release additional byte credit.
    const auto pending_bytes = flight.PendingBytes();
    flight.Observe({.elapsed_us = 450000, .complete_frames = 6, .complete_bytes = 192000, .latest_frame_index = 6},
                   started + std::chrono::milliseconds(450));
    EXPECT_EQ(flight.PendingBytes(), pending_bytes);
    flight.Observe({.elapsed_us = 470000, .complete_frames = 7, .complete_bytes = 224000, .latest_frame_index = 7},
                   started + std::chrono::milliseconds(470));
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(470)));
}

TEST(IrohVideoFrameRate, QualityReductionPrecedesFrameSheddingAndHealthyPathRestoresConfiguredRate) {
    VideoFrameRate rate{};
    const auto start = VideoFrameRate::Clock::time_point{};
    EXPECT_EQ(rate.Limit(120), 120);
    rate.Observe(4000000, true, start);
    rate.Observe(4000000, true, start + std::chrono::seconds(10));
    EXPECT_EQ(rate.Limit(60), 60);
    rate.Observe(250000, true, start + std::chrono::seconds(11));
    rate.Observe(250000, true, start + std::chrono::seconds(12));
    EXPECT_EQ(rate.Limit(60), 30);
    rate.Observe(250000, true, start + std::chrono::seconds(14));
    EXPECT_EQ(rate.Limit(60), 15);
    EXPECT_EQ(rate.Limit(10), 10);
    for (int second{15}; second <= 25; ++second) rate.Observe(250000, false, start + std::chrono::seconds(second));
    EXPECT_EQ(rate.Limit(60), 60);
    EXPECT_EQ(rate.Limit(120), 120);
}

struct MediaProbe final {
    std::mutex mutex{};
    std::condition_variable changed{};
    std::vector<media::VideoFrame> video{};
    std::vector<media::Packet> audio{};
    std::vector<media::VideoRecoveryRequest> recovery{};
    std::vector<VideoReceiveFeedback> feedback{};
    std::size_t sends_completed{};
    std::size_t sends_failed{};
};

class IrohMediaTest : public ::testing::Test {
protected:
    void SetUp() override {
        runtime_ = PxAsyncRuntime::Create();
        ASSERT_TRUE(runtime_->Start());
        server_ = Endpoint::Bind(transport::testing::EndpointConfiguration(), 5000);
        client_ = Endpoint::Bind(transport::testing::EndpointConfiguration(), 5000);
        ASSERT_TRUE(server_);
        ASSERT_TRUE(client_);
        const auto address = server_->Address();
        ASSERT_TRUE(address);
        auto pending = std::async(std::launch::async, [server = server_] { return server->Accept(5000); });
        incoming_ = client_->Connect(*address, 5000);
        outgoing_ = pending.get();
        ASSERT_TRUE(incoming_);
        ASSERT_TRUE(outgoing_);
        sender_ = std::make_shared<MediaDatagramSender>(outgoing_, runtime_->Executor(PxAsyncLane::kWorker));
        MediaReceiveCallbacks callbacks{};
        callbacks.video = [probe = probe_](media::VideoFrame frame) {
            std::lock_guard lock(probe->mutex);
            probe->video.push_back(std::move(frame));
            probe->changed.notify_all();
        };
        callbacks.audio = [probe = probe_](media::AudioDelivery audio) {
            const auto opus = media::UnwrapAudioPayload(audio.payload);
            if (!opus) return;
            std::lock_guard lock(probe->mutex);
            probe->audio.push_back(*opus);
            probe->changed.notify_all();
        };
        callbacks.recovery = [probe = probe_](media::VideoRecoveryRequest request) {
            std::lock_guard lock(probe->mutex);
            probe->recovery.push_back(std::move(request));
            probe->changed.notify_all();
        };
        callbacks.feedback = [probe = probe_](VideoReceiveFeedback report) {
            std::lock_guard lock(probe->mutex);
            probe->feedback.push_back(report);
            probe->changed.notify_all();
        };
        receiver_ = std::make_shared<MediaDatagramReceiver>(incoming_, std::move(callbacks));
        ASSERT_TRUE(receiver_->Start());
    }
    void TearDown() override {
        if (sender_) sender_->Stop();
        if (receiver_) receiver_->Stop();
        if (outgoing_) outgoing_->Close();
        if (incoming_) incoming_->Close();
        if (server_) server_->Close();
        if (client_) client_->Close();
        if (runtime_) {
            runtime_->RequestStop();
            runtime_->Join();
        }
    }
    media::VideoFrame Frame(std::uint64_t index = 7) {
        return {.kind = media::VideoFrameKind::kIdr,
                .stream = 1,
                .width = 1920,
                .height = 1080,
                .frame_index = index,
                .monitor = "test-monitor",
                .encoded = media::Packet(150000, 0x53)};
    }
    std::function<void(bool)> Completion() {
        return [probe = probe_](bool delivered) {
            std::lock_guard lock(probe->mutex);
            ++probe->sends_completed;
            if (!delivered) ++probe->sends_failed;
            probe->changed.notify_all();
        };
    }
    std::shared_ptr<PxAsyncRuntime> runtime_{};
    std::shared_ptr<Endpoint> server_{};
    std::shared_ptr<Endpoint> client_{};
    std::shared_ptr<Connection> outgoing_{};
    std::shared_ptr<Connection> incoming_{};
    std::shared_ptr<MediaDatagramSender> sender_{};
    std::shared_ptr<MediaDatagramReceiver> receiver_{};
    std::shared_ptr<MediaProbe> probe_{std::make_shared<MediaProbe>()};
};

TEST_F(IrohMediaTest, QuicCapacityMatchesAdmissionAndCaptureStopsBeforeAThirdEncodedFrame) {
    ASSERT_EQ(outgoing_->Snapshot().datagram_buffer_space, VideoQueueAdmission::kTransportBufferBytes);
    const auto stalled = std::make_shared<asio::io_context>();
    const auto sender = std::make_shared<MediaDatagramSender>(outgoing_, stalled->get_executor());
    auto frame = Frame();
    frame.encoded.resize(9000);
    ASSERT_TRUE(sender->CanEncodeVideo(frame.stream));
    ASSERT_TRUE(sender->SendVideo(frame, Completion()));
    ASSERT_TRUE(sender->CanEncodeVideo(frame.stream));
    ++frame.frame_index;
    ASSERT_TRUE(sender->SendVideo(frame, Completion()));
    EXPECT_FALSE(sender->CanEncodeVideo(frame.stream));
    EXPECT_FALSE(sender->CanEncodeVideo(frame.stream, std::chrono::steady_clock::now()));
    sender->Stop();
    sender->Stop();
    stalled->run();
    EXPECT_EQ(probe_->sends_completed, 2);
    EXPECT_EQ(probe_->sends_failed, 2);
}

TEST_F(IrohMediaTest, CumulativeDatagramsReleaseCreditDespiteSkippedReorderedReportsAndUnreadControl) {
    // Leave a reliable control record unread. Replaceable feedback has its own datagram path.
    const auto control = Channel::Open(incoming_, ChannelKind::kControl, 1000);
    ASSERT_TRUE(control);
    ASSERT_TRUE((*control)->Send(media::Packet(65536, 0x53), 1000));
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::now();
    for (std::uint64_t frame_index{1}; frame_index <= 8; ++frame_index) flight.Sent(0, frame_index, 32768, started);
    ASSERT_FALSE(flight.CanSend(0, 1000, started));
    // Reports for frames 1 through 7 were lost; the cumulative eighth report clears all debt.
    const auto newest = EncodeVideoFeedbackDatagram({.elapsed_us = 400000, .complete_frames = 8, .complete_bytes = 262144, .latest_frame_index = 8});
    ASSERT_TRUE(incoming_->SendDatagram(newest, 1000));
    const auto received = outgoing_->ReceiveDatagram(2000);
    ASSERT_TRUE(received);
    const auto report = DecodeVideoFeedbackDatagram(*received);
    ASSERT_TRUE(report);
    flight.Observe(*report, started);
    EXPECT_EQ(flight.PendingBytes(), 0);
    EXPECT_TRUE(flight.CanSend(0, 1000, started));
    flight.Sent(0, 9, 1000, started);
    const auto delayed = EncodeVideoFeedbackDatagram({.elapsed_us = 350000, .complete_frames = 7, .complete_bytes = 229376, .latest_frame_index = 7});
    ASSERT_TRUE(incoming_->SendDatagram(delayed, 1000));
    const auto received_delayed = outgoing_->ReceiveDatagram(2000);
    ASSERT_TRUE(received_delayed);
    const auto delayed_report = DecodeVideoFeedbackDatagram(*received_delayed);
    ASSERT_TRUE(delayed_report);
    flight.Observe(*delayed_report, started);
    EXPECT_EQ(flight.PendingBytes(), 1000);
}

TEST_F(IrohMediaTest, PacedVideoAndAudioUseActualQuicDatagrams) {
    const auto frame = Frame();
    ASSERT_TRUE(sender_->SendVideo(frame, Completion()));
    const media::Packet opus(100, 0x37);
    for (std::size_t packet_index{}; packet_index < 8; ++packet_index) ASSERT_TRUE(sender_->SendAudio(opus));
    std::unique_lock lock(probe_->mutex);
    ASSERT_TRUE(
        probe_->changed.wait_for(lock, std::chrono::seconds(3),
                                 [probe = probe_] { return probe->video.size() == 1 && probe->audio.size() >= 4 && probe->sends_completed == 1; }))
        << "video=" << probe_->video.size() << " audio=" << probe_->audio.size() << " sends_completed=" << probe_->sends_completed
        << " sends_failed=" << probe_->sends_failed << " recovery_requests=" << probe_->recovery.size();
    EXPECT_EQ(probe_->video.front().encoded, frame.encoded);
    EXPECT_EQ(probe_->video.front().monitor, frame.monitor);
    EXPECT_EQ(probe_->video.front().frame_index, frame.frame_index);
    EXPECT_EQ(probe_->audio.front(), opus);
    EXPECT_EQ(probe_->sends_failed, 0);
    ASSERT_TRUE(probe_->changed.wait_for(lock, std::chrono::seconds(2),
                                         [probe = probe_] { return !probe->feedback.empty() && probe->feedback.back().complete_frames == 1; }));
    EXPECT_EQ(probe_->feedback.back().complete_frames, 1);
    EXPECT_EQ(probe_->feedback.back().complete_bytes, frame.encoded.size());
    EXPECT_EQ(probe_->feedback.back().latest_frame_index, frame.frame_index);
}

TEST(IrohVoiceDatagram, RejectsNonVoiceTruncatedAndOversizedMessages) {
    Message voice{};
    voice.set_type(kVoiceAudioFrame);
    auto& frame = *voice.mutable_voice_audio_frame();
    frame.set_call_id("voice-call");
    frame.set_sequence(17);
    frame.set_capture_time_ms(2345);
    frame.set_opus(std::string(160, 'v'));
    const auto packet = EncodeVoiceDatagram(voice);
    ASSERT_FALSE(packet.empty());
    const auto decoded = DecodeVoiceDatagram(packet);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->SerializeAsString(), voice.SerializeAsString());
    EXPECT_FALSE(DecodeVoiceDatagram(std::span<const std::uint8_t>{packet}.first(4)));
    auto malformed = packet;
    malformed[3] = 2;
    EXPECT_FALSE(DecodeVoiceDatagram(malformed));
    voice.set_type(kKeyEvent);
    EXPECT_TRUE(EncodeVoiceDatagram(voice).empty());
    voice.set_type(kVoiceAudioFrame);
    frame.set_opus(std::string(1275, 'v'));
    EXPECT_TRUE(EncodeVoiceDatagram(voice).empty());
    EXPECT_FALSE(DecodeVoiceDatagram(Bytes(kMediaDatagramBytes + 1, 0)));
}

TEST_F(IrohMediaTest, FecRecoversDroppedShardsAndRetainsReferenceRecoveryKind) {
    for (std::uint32_t frame_number{1}; frame_number <= 2; ++frame_number) {
        auto frame = Frame(frame_number == 1 ? 9 : 13);
        frame.encoded.resize(18000);
        frame.kind = frame_number == 1 ? media::VideoFrameKind::kIdr : media::VideoFrameKind::kReferenceRecovery;
        const auto packets = media::PacketizeVideoFrame(frame, {.frame_index = frame_number, .datagram_size = kMediaDatagramBytes});
        ASSERT_TRUE(packets);
        // Drop the first two shards, including protected metadata, on the actual QUIC path.
        for (std::size_t packet_index{2}; packet_index < packets->packets.size(); ++packet_index) {
            ASSERT_LE(packets->packets[packet_index].size(), outgoing_->DatagramLimit());
            ASSERT_TRUE(outgoing_->SendDatagram(packets->packets[packet_index]));
        }
        std::unique_lock lock(probe_->mutex);
        ASSERT_TRUE(
            probe_->changed.wait_for(lock, std::chrono::seconds(3), [probe = probe_, frame_number] { return probe->video.size() == frame_number; }));
        EXPECT_EQ(probe_->video.back().encoded, frame.encoded);
    }
    EXPECT_EQ(probe_->video.back().kind, media::VideoFrameKind::kReferenceRecovery);
    EXPECT_EQ(probe_->video.back().preceding_frame_index, 9);
}

TEST_F(IrohMediaTest, BoundedFrameQueueExpiresWithoutBlockingReliableTraffic) {
    const auto stalled = std::make_shared<asio::io_context>();
    const auto sender =
        std::make_shared<MediaDatagramSender>(outgoing_, stalled->get_executor(), MediaSendOptions{.frame_deadline = std::chrono::milliseconds(1)});
    auto frame = Frame();
    frame.encoded.resize(9000);  // Isolate the local frame-count/deadline limit from the remote byte window.
    ASSERT_TRUE(sender->SendVideo(frame, Completion()));
    for (std::uint64_t frame_index{8}; frame_index <= 10; ++frame_index) {
        frame.frame_index = frame_index;
        ASSERT_TRUE(sender->SendVideo(frame, Completion()));
    }
    frame.frame_index = 11;
    EXPECT_FALSE(sender->SendVideo(frame, Completion()));
    const auto stream = outgoing_->OpenStream(100, 3000);
    ASSERT_TRUE(stream);
    const Bytes control{1, 2, 3};
    ASSERT_TRUE(stream->Write(control, 3000));
    const auto accepted = incoming_->AcceptStream(3000);
    ASSERT_TRUE(accepted);
    const auto received_control = accepted->Read(10, 3000);
    ASSERT_TRUE(received_control);
    EXPECT_EQ(*received_control, control);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    stalled->run();
    EXPECT_EQ(probe_->sends_completed, 4);
    EXPECT_EQ(probe_->sends_failed, 4);
    EXPECT_TRUE(probe_->video.empty());
}

TEST_F(IrohMediaTest, BackpressureDropRemainsVisibleToReferenceRecovery) {
    const auto stalled = std::make_shared<asio::io_context>();
    const auto sender = std::make_shared<MediaDatagramSender>(outgoing_, stalled->get_executor());
    auto frame = Frame();
    frame.encoded.resize(9000);
    ASSERT_TRUE(sender->SendVideo(frame, Completion()));
    frame.kind = media::VideoFrameKind::kPredicted;
    for (std::uint64_t frame_index{8}; frame_index <= 10; ++frame_index) {
        frame.frame_index = frame_index;
        ASSERT_TRUE(sender->SendVideo(frame, Completion()));
    }
    frame.frame_index = 11;
    EXPECT_FALSE(sender->SendVideo(frame, Completion()));
    stalled->run();
    {
        std::unique_lock lock(probe_->mutex);
        ASSERT_TRUE(probe_->changed.wait_for(lock, std::chrono::seconds(3), [probe = probe_] { return probe->video.size() == 4; }));
    }
    stalled->restart();
    frame.frame_index = 12;
    ASSERT_TRUE(sender->SendVideo(frame, Completion()));
    stalled->run();
    {
        std::unique_lock lock(probe_->mutex);
        ASSERT_TRUE(probe_->changed.wait_for(lock, std::chrono::seconds(3), [probe = probe_] { return !probe->recovery.empty(); }));
        EXPECT_EQ(probe_->recovery.front().invalid_reference_frame, 11);
        EXPECT_EQ(probe_->video.size(), 4);
        EXPECT_EQ(probe_->sends_completed, 5);
    }
}

TEST_F(IrohMediaTest, EncodedFrameCrossingFlightBudgetPreservesReferenceChainAndPausesNextCapture) {
    auto frame = Frame();
    // Cross 256 KiB with or without direct-path FEC; Relay omits parity.
    for (std::size_t delivered_frames{1}; delivered_frames <= 2; ++delivered_frames) {
        frame.encoded.resize(delivered_frames == 1 ? 180000 : 100000);
        frame.frame_index = delivered_frames;
        ASSERT_TRUE(sender_->CanEncodeVideo(frame.stream));
        ASSERT_TRUE(sender_->SendVideo(frame, Completion()));
        std::unique_lock lock(probe_->mutex);
        ASSERT_TRUE(probe_->changed.wait_for(lock, std::chrono::seconds(3),
                                             [probe = probe_, delivered_frames] { return probe->video.size() == delivered_frames; }));
        frame.kind = media::VideoFrameKind::kPredicted;
    }
    EXPECT_FALSE(sender_->CanEncodeVideo(frame.stream));
    const auto recovery_request = std::chrono::steady_clock::now();
    EXPECT_TRUE(sender_->CanEncodeVideo(frame.stream, recovery_request));
    EXPECT_FALSE(sender_->CanEncodeVideo(frame.stream, recovery_request));
    EXPECT_FALSE(sender_->CanEncodeVideo(frame.stream, recovery_request - std::chrono::milliseconds(1)));
    EXPECT_FALSE(sender_->CanEncodeVideo(frame.stream));
    sender_->ObserveFeedback({.stream = frame.stream, .elapsed_us = 50000, .complete_frames = 2, .latest_frame_index = 2});
    ASSERT_TRUE(sender_->CanEncodeVideo(frame.stream));
    frame.frame_index = 1000;
    ASSERT_TRUE(sender_->SendVideo(frame, Completion()));
    std::unique_lock lock(probe_->mutex);
    ASSERT_TRUE(probe_->changed.wait_for(lock, std::chrono::seconds(3), [probe = probe_] { return probe->video.size() == 3; }));
    EXPECT_TRUE(probe_->recovery.empty());
    EXPECT_EQ(probe_->sends_failed, 0);
}

TEST(IrohVideoFlight, ReceiptThroughputBoundsActiveWeakLinkBacklogAndDrainedPathRecovers) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    flight.Sent(0, 0, 1000, started);
    flight.Observe({.elapsed_us = 50000, .complete_frames = 1, .latest_frame_index = 0}, started + std::chrono::milliseconds(5));
    for (std::uint64_t frame_index{1}; frame_index <= 10; ++frame_index) flight.Sent(0, frame_index, 6000, started);
    flight.Sent(0, 11, 6000, started + std::chrono::milliseconds(200));
    flight.Observe({.elapsed_us = 300000, .complete_frames = 11, .latest_frame_index = 10}, started + std::chrono::milliseconds(300));
    for (std::uint64_t frame_index{12}; frame_index <= 16; ++frame_index) {
        EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(301)));
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(301));
    }
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(302)).budget_bytes, 32768);
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(302)));
    flight.Observe({.elapsed_us = 500000, .complete_frames = 17, .latest_frame_index = 16}, started + std::chrono::milliseconds(500));
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(500)));
    flight.Sent(0, 17, 50000, started + std::chrono::milliseconds(501));
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(502)));
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(502)).budget_bytes, 262144);
    for (std::uint64_t frame_index{18}; frame_index <= 23; ++frame_index) {
        flight.Sent(0, frame_index, 50000, started + std::chrono::milliseconds(950));
    }
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(951)));
    flight.Observe({.elapsed_us = 1000000, .complete_frames = 18, .latest_frame_index = 17}, started + std::chrono::milliseconds(1000));
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(1001)).budget_bytes, 200000);
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(1001)));
    flight.Observe({.elapsed_us = 1050000, .complete_frames = 24, .latest_frame_index = 23}, started + std::chrono::milliseconds(1050));
    flight.Sent(0, 24, 1000, started + std::chrono::milliseconds(1051));
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(1052)).budget_bytes, 262144);
}

TEST(IrohVideoFlight, DrainedFlightWindowDoesNotCarryOldCongestionIntoTheNextBurst) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    flight.Sent(0, 1, 6000, started);
    flight.Observe({.elapsed_us = 50000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(50));
    flight.Sent(0, 2, 6000, started + std::chrono::milliseconds(50));
    for (std::uint64_t frame_index{3}; frame_index <= 8; ++frame_index) {
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(250));
    }
    flight.Observe({.elapsed_us = 300000, .complete_frames = 2, .latest_frame_index = 2}, started + std::chrono::milliseconds(300));
    ASSERT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(301)));
    // All outstanding work completed. The report's generation time still
    // includes the earlier stall; it cannot constrain a new prediction chain.
    flight.Observe({.elapsed_us = 600000, .complete_frames = 8, .latest_frame_index = 8}, started + std::chrono::milliseconds(600));
    ASSERT_EQ(flight.PendingBytes(), 0);
    for (std::uint64_t frame_index{9}; frame_index <= 14; ++frame_index) {
        ASSERT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(601)));
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(601));
    }
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(602)));
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(602)).budget_bytes, 262144);
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(602)).forward_delay_growth_us, 0);
}

TEST(IrohVideoFlight, PartialProgressKeepsCongestionDebtAndASecondStallStillConstrainsCapture) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.Sent(0, 1, 6000, started);
    flight.Observe({.elapsed_us = 50000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(50));
    flight.Sent(0, 2, 6000, started + std::chrono::milliseconds(50));
    for (std::uint64_t frame_index{3}; frame_index <= 8; ++frame_index) {
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(250));
    }
    flight.Observe({.elapsed_us = 300000, .complete_frames = 2, .latest_frame_index = 2}, started + std::chrono::milliseconds(300));
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(301)));
    EXPECT_EQ(flight.PendingBytes(), 36000);
    // A non-advancing periodic report must not erase debt or recover capacity.
    flight.Observe({.elapsed_us = 350000, .complete_frames = 2, .latest_frame_index = 2}, started + std::chrono::milliseconds(350));
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(351)));
    EXPECT_EQ(flight.PendingBytes(), 36000);
    flight.Observe({.elapsed_us = 600000, .complete_frames = 8, .latest_frame_index = 8}, started + std::chrono::milliseconds(600));
    flight.Sent(0, 9, 6000, started + std::chrono::milliseconds(601));
    for (std::uint64_t frame_index{10}; frame_index <= 16; ++frame_index) {
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(1100));
    }
    flight.Observe({.elapsed_us = 1150000, .complete_frames = 9, .latest_frame_index = 9}, started + std::chrono::milliseconds(1150));
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(1151)));
}

TEST(IrohVideoFlight, ReverseFeedbackDelayDoesNotShrinkHealthyForwardCapacity) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    flight.Sent(0, 1, 6000, started);
    flight.Observe({.elapsed_us = 50000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(50));
    flight.Sent(0, 2, 6000, started + std::chrono::milliseconds(250));
    for (std::uint64_t frame_index{3}; frame_index <= 8; ++frame_index) {
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(450));
    }
    // Receiver generated this report after the same 50 ms forward interval.
    // Only its return trip gained 200 ms. Outstanding video must not force
    // capture to pause against an incorrectly reduced 32 KiB window.
    flight.Observe({.elapsed_us = 300000, .complete_frames = 2, .latest_frame_index = 2}, started + std::chrono::milliseconds(500));
    EXPECT_GT(flight.Inspect(0, started + std::chrono::milliseconds(501)).budget_bytes, 262144);
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(501)));
}

TEST(IrohVideoFlight, DelayedReturnProgressDoesNotPauseTwentyMegabitCaptureAtTheOldFixedLimit) {
    VideoFlightWindow flight{VideoSendBudget{20'000'000}};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    flight.Sent(0, 1, 6000, started);
    flight.Observe({.elapsed_us = 50000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(50));
    for (std::uint64_t frame_index{2}; frame_index <= 15; ++frame_index) {
        flight.Sent(0, frame_index, 20900, started + std::chrono::milliseconds(50 + (frame_index - 2) * 16));
    }
    // The receiver advances after 50ms, but its return report takes another
    // 210ms. Replay the live 271700-byte debt with an empty local QUIC queue.
    flight.Observe({.elapsed_us = 100000, .complete_frames = 2, .latest_frame_index = 2}, started + std::chrono::milliseconds(310));
    EXPECT_EQ(flight.PendingBytes(), 271700);
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(311)).forward_delay_growth_us, 0);
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(311)).return_delay_growth_us, 210000);
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(311)));
}

TEST(IrohVideoFlight, DelayedReturnAllowanceRemainsBoundedAndStaleProgressCannotEnlargeIt) {
    VideoFlightWindow flight{VideoSendBudget{200'000'000}};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(600000);
    flight.Sent(0, 1, 6000, started);
    flight.Observe({.elapsed_us = 50000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(50));
    for (std::uint64_t frame_index{2}; frame_index <= 41; ++frame_index) {
        flight.Sent(0, frame_index, 30000, started + std::chrono::milliseconds(60));
    }
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(61)).budget_bytes, 1024 * 1024);
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(61)));
    flight.Observe({.elapsed_us = 100000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(250));
    EXPECT_EQ(flight.PendingBytes(), 1200000);
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(251)).return_delay_growth_us, 0);
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(251)));
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(310)));
}

TEST(IrohVideoFlight, ForwardCongestionStillConstrainsCaptureWithDelayedReturnFeedback) {
    VideoFlightWindow flight{VideoSendBudget{20'000'000}};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    flight.Sent(0, 1, 6000, started);
    flight.Observe({.elapsed_us = 50000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(50));
    flight.Sent(0, 2, 6000, started + std::chrono::milliseconds(50));
    for (std::uint64_t frame_index{3}; frame_index <= 9; ++frame_index) {
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(500));
    }
    flight.Observe({.elapsed_us = 450000, .complete_frames = 2, .latest_frame_index = 2}, started + std::chrono::milliseconds(650));
    const auto snapshot = flight.Inspect(0, started + std::chrono::milliseconds(651));
    EXPECT_EQ(snapshot.return_delay_growth_us, 200000);
    EXPECT_GT(snapshot.forward_delay_growth_us, 100000);
    EXPECT_LE(snapshot.budget_bytes, 256 * 1024);
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(651)));
}

TEST(IrohVideoFlight, FeedbackSilenceDoesNotPauseBeforeBoundedCreditIsConsumed) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    for (std::uint64_t frame_index{1}; frame_index <= 20; ++frame_index) {
        flight.Sent(0, frame_index, 5000, started + std::chrono::milliseconds((frame_index - 1) * 16));
    }
    // Live regression: about 300 ms without receipts, 20 frames / 100 KiB,
    // while the forward video path is healthy. A separate 200 ms age gate
    // must not exhaust the still-available 256 KiB flight budget.
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(320)));
    for (std::uint64_t frame_index{21}; frame_index <= 64; ++frame_index) {
        flight.Sent(0, frame_index, 1000, started + std::chrono::milliseconds(320));
    }
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(321)));
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(570)));
}

TEST(IrohVideoFlight, ForwardDelayStillShrinksCapacityWithPromptReturnFeedback) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    flight.Sent(0, 1, 6000, started);
    flight.Observe({.elapsed_us = 50000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(50));
    flight.Sent(0, 2, 6000, started + std::chrono::milliseconds(50));
    for (std::uint64_t frame_index{3}; frame_index <= 8; ++frame_index) {
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(250));
    }
    // The same report now describes 200 ms of additional forward backlog.
    flight.Observe({.elapsed_us = 300000, .complete_frames = 2, .latest_frame_index = 2}, started + std::chrono::milliseconds(300));
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(301)).budget_bytes, 32768);
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(301)));
}

TEST(IrohVideoFlight, HealthyStaticSceneDoesNotShrinkCapacityForFollowingMotion) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.Sent(0, 1, 1000, started);
    flight.Observe({.elapsed_us = 250000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(5));
    flight.Sent(0, 2, 1000, started + std::chrono::milliseconds(250));
    flight.Observe({.elapsed_us = 500000, .complete_frames = 2, .latest_frame_index = 2}, started + std::chrono::milliseconds(255));
    flight.Sent(0, 3, 1000, started + std::chrono::milliseconds(256));
    EXPECT_EQ(flight.Inspect(0, started + std::chrono::milliseconds(257)).budget_bytes, 262144);
    EXPECT_TRUE(flight.CanSend(0, 100000, started + std::chrono::milliseconds(257)));
}

TEST(IrohVideoFlight, RecoveredForwardProgressReleasesCapacityDespiteDelayedReturnFeedback) {
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(30000);
    flight.Sent(0, 1, 6000, started);
    flight.Observe({.elapsed_us = 50000, .complete_frames = 1, .latest_frame_index = 1}, started + std::chrono::milliseconds(50));
    flight.Sent(0, 2, 6000, started + std::chrono::milliseconds(50));
    for (std::uint64_t frame_index{3}; frame_index <= 8; ++frame_index) {
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(250));
    }
    flight.Observe({.elapsed_us = 300000, .complete_frames = 2, .latest_frame_index = 2}, started + std::chrono::milliseconds(300));
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(301)));
    // A sparse probe confirms that forward delay has fallen to 40 ms. Its
    // return acknowledgement is late, with subsequent video still outstanding.
    flight.Sent(0, 9, 6000, started + std::chrono::milliseconds(500));
    for (std::uint64_t frame_index{10}; frame_index <= 15; ++frame_index) {
        flight.Sent(0, frame_index, 6000, started + std::chrono::milliseconds(740));
    }
    flight.Observe({.elapsed_us = 590000, .complete_frames = 9, .latest_frame_index = 9}, started + std::chrono::milliseconds(750));
    EXPECT_TRUE(flight.CanSend(0, 0, started + std::chrono::milliseconds(751)));
    EXPECT_EQ(flight.PendingBytes(), 36000);
    // Recovery does not discard debt or disable congestion control. A new
    // forward stall must constrain the same bounded window again.
    flight.Observe({.elapsed_us = 1040000, .complete_frames = 10, .latest_frame_index = 10}, started + std::chrono::milliseconds(1050));
    flight.Sent(0, 16, 6000, started + std::chrono::milliseconds(1051));
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(1052)));
}

TEST_F(IrohMediaTest, SmallFrameBurstAndDelayedReceiptPreserveTheEncodedReferenceChain) {
    auto frame = Frame();
    frame.encoded.resize(9000);
    for (std::size_t delivered_frames{1}; delivered_frames <= 8; ++delivered_frames) {
        frame.frame_index = 6 + delivered_frames;
        ASSERT_TRUE(sender_->CanEncodeVideo(frame.stream));
        ASSERT_TRUE(sender_->SendVideo(frame, Completion()));
        std::unique_lock lock(probe_->mutex);
        ASSERT_TRUE(probe_->changed.wait_for(lock, std::chrono::seconds(3),
                                             [probe = probe_, delivered_frames] { return probe->video.size() == delivered_frames; }));
        frame.kind = media::VideoFrameKind::kPredicted;
    }
    // Eight small frames no longer exhaust a separate frame quota. Receipt still
    // releases their byte debt; skipped capture indices must preserve references.
    EXPECT_TRUE(sender_->CanEncodeVideo(frame.stream));
    sender_->ObserveFeedback({.stream = frame.stream, .elapsed_us = 50000, .complete_frames = 8, .latest_frame_index = 14});
    ASSERT_TRUE(sender_->CanEncodeVideo(frame.stream));
    frame.frame_index = 1000;  // Capture frames skipped before encoding have no transport/reference gap.
    ASSERT_TRUE(sender_->SendVideo(frame, Completion()));
    std::unique_lock lock(probe_->mutex);
    ASSERT_TRUE(probe_->changed.wait_for(lock, std::chrono::seconds(3), [probe = probe_] { return probe->video.size() == 9; }));
    EXPECT_TRUE(probe_->recovery.empty());
    EXPECT_EQ(probe_->video.back().frame_index, 1000);
}

TEST_F(IrohMediaTest, StopFromCompletionCancelsQueuedFramesExactlyOnce) {
    const auto stalled = std::make_shared<asio::io_context>();
    const auto sender = std::make_shared<MediaDatagramSender>(outgoing_, stalled->get_executor());
    auto frame = Frame();
    frame.encoded.resize(9000);
    ASSERT_TRUE(sender->SendVideo(frame, [owner = std::weak_ptr{sender}, completion = Completion()](bool delivered) {
        completion(delivered);
        if (const auto sender = owner.lock()) sender->Stop();
    }));
    for (std::uint64_t frame_index{8}; frame_index <= 10; ++frame_index) {
        frame.frame_index = frame_index;
        ASSERT_TRUE(sender->SendVideo(frame, Completion()));
    }
    stalled->run();
    sender->Stop();
    EXPECT_EQ(probe_->sends_completed, 4);
    EXPECT_EQ(probe_->sends_failed, 3);
    EXPECT_FALSE(sender->SendVideo(frame, Completion()));
    EXPECT_FALSE(outgoing_->IsClosed());
}

TEST_F(IrohMediaTest, ReceiverStopsFromVideoCallbackWithoutClosingReliableConnection) {
    receiver_->Stop();
    struct CallbackState final {
        std::weak_ptr<MediaDatagramReceiver> receiver{};
        std::promise<void> stopped{};
    };
    const auto state = std::make_shared<CallbackState>();
    auto stopped = state->stopped.get_future();
    MediaReceiveCallbacks callbacks{};
    callbacks.video = [state](media::VideoFrame) {
        if (const auto receiver = state->receiver.lock()) receiver->Stop();
        state->stopped.set_value();
    };
    receiver_ = std::make_shared<MediaDatagramReceiver>(incoming_, std::move(callbacks));
    state->receiver = receiver_;
    ASSERT_TRUE(receiver_->Start());
    ASSERT_TRUE(sender_->SendVideo(Frame(), Completion()));
    const auto callback_status = stopped.wait_for(std::chrono::seconds(3));
    {
        std::lock_guard lock(probe_->mutex);
        ASSERT_EQ(callback_status, std::future_status::ready)
            << "sends_completed=" << probe_->sends_completed << " sends_failed=" << probe_->sends_failed;
    }
    EXPECT_FALSE(incoming_->IsClosed());
    receiver_->Stop();
}
}  // namespace
}  // namespace px::transport
