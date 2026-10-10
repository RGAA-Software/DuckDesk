#include <gtest/gtest.h>

#include <condition_variable>
#include <future>
#include <thread>

#include "channel.h"
#include "compact_audio_datagram.h"
#include "endpoint_configuration.h"
#include "media_datagrams.h"
#include "px_common/async_runtime.h"
#include "video_feedback_datagram.h"
#include "video_frame_rate.h"
#include "video_rate_control.h"

namespace px::transport {
namespace {
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
    const auto recovered = control.Update(14'000'000, path, start + std::chrono::milliseconds(3500));
    EXPECT_GT(recovered, 3'500'000);
    EXPECT_LT(recovered, 4'500'000);
    EXPECT_EQ(control.Update(14'000'000, path, start + std::chrono::seconds(4)), recovered);
    EXPECT_EQ(control.Update(2'000'000, path, start + std::chrono::milliseconds(4100)), 2'000'000);
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
    EXPECT_GT(control.Update(8'000'000, path, start + std::chrono::seconds(22)), reduced_rate);
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

TEST(IrohVideoFlight, LocalCompletionDoesNotReleaseRemoteCreditAndMissingFramesCanProbe) {
    VideoFlightWindow flight{};
    const auto start = VideoFlightWindow::Clock::time_point{};
    flight.ObserveRtt(5000);
    for (std::uint64_t frame_index{1}; frame_index <= 8; ++frame_index) {
        EXPECT_TRUE(flight.CanSend(0, 10000, start));
        flight.Sent(0, frame_index, 10000, start);
    }
    EXPECT_FALSE(flight.CanSend(0, 10000, start + std::chrono::milliseconds(100)));
    EXPECT_EQ(flight.PendingBytes(), 80000);
    EXPECT_TRUE(flight.CanSend(0, 10000, start + std::chrono::milliseconds(250)));
    flight.Sent(0, 9, 10000, start + std::chrono::milliseconds(250));
    EXPECT_FALSE(flight.CanSend(0, 10000, start + std::chrono::milliseconds(251)));
    flight.Observe({.elapsed_us = 300000, .complete_frames = 1, .latest_frame_index = 9}, start + std::chrono::milliseconds(300));
    EXPECT_EQ(flight.PendingBytes(), 0);
    EXPECT_TRUE(flight.CanSend(0, 10000, start + std::chrono::milliseconds(301)));
}

TEST(IrohVideoFlight, ByteAgeAndRttBoundsPreserveStaticFramesAndRejectStaleFeedback) {
    VideoFlightWindow flight{};
    const auto start = VideoFlightWindow::Clock::time_point{};
    flight.Sent(0, 1, 200000, start);
    EXPECT_FALSE(flight.CanSend(0, 100000, start));
    EXPECT_FALSE(flight.CanSend(0, 1000, start + std::chrono::milliseconds(210)));
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
        flight.Sent(0, frame_index, 32000, started + std::chrono::milliseconds((frame_index - 1) * 16));
    }
    const auto progress_time = started + std::chrono::milliseconds(240);
    ASSERT_FALSE(flight.CanSend(0, 0, progress_time));
    // Matches the live trace: 8 -> 7 outstanding frames, capacity is free,
    // but the remaining oldest frame is 224ms old and the probe is not due.
    flight.Observe({.elapsed_us = 240000, .complete_frames = 1, .complete_bytes = 32000, .latest_frame_index = 1}, progress_time);
    EXPECT_EQ(flight.PendingBytes(), 224000);
    EXPECT_TRUE(flight.CanSend(0, 0, progress_time));
    flight.Observe({.elapsed_us = 260000, .complete_frames = 6, .complete_bytes = 192000, .latest_frame_index = 6},
                   started + std::chrono::milliseconds(260));
    flight.Sent(0, 9, 32000, started + std::chrono::milliseconds(261));
    // Repeating the same complete frame cannot hide a real stall or release new credit.
    flight.Observe({.elapsed_us = 450000, .complete_frames = 6, .complete_bytes = 192000, .latest_frame_index = 6},
                   started + std::chrono::milliseconds(450));
    EXPECT_FALSE(flight.CanSend(0, 0, started + std::chrono::milliseconds(461)));
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

TEST_F(IrohMediaTest, CumulativeDatagramsReleaseCreditDespiteSkippedReorderedReportsAndUnreadControl) {
    // Leave a reliable control record unread. Replaceable feedback has its own datagram path.
    const auto control = Channel::Open(incoming_, ChannelKind::kControl, 1000);
    ASSERT_TRUE(control);
    ASSERT_TRUE((*control)->Send(media::Packet(65536, 0x53), 1000));
    VideoFlightWindow flight{};
    const auto started = VideoFlightWindow::Clock::now();
    for (std::uint64_t frame_index{1}; frame_index <= 8; ++frame_index) flight.Sent(0, frame_index, 1000, started);
    ASSERT_FALSE(flight.CanSend(0, 1000, started));
    // Reports for frames 1 through 7 were lost; the cumulative eighth report clears all debt.
    const auto newest = EncodeVideoFeedbackDatagram({.elapsed_us = 400000, .complete_frames = 8, .complete_bytes = 8000, .latest_frame_index = 8});
    ASSERT_TRUE(incoming_->SendDatagram(newest, 1000));
    const auto received = outgoing_->ReceiveDatagram(2000);
    ASSERT_TRUE(received);
    const auto report = DecodeVideoFeedbackDatagram(*received);
    ASSERT_TRUE(report);
    flight.Observe(*report, started);
    EXPECT_EQ(flight.PendingBytes(), 0);
    EXPECT_TRUE(flight.CanSend(0, 1000, started));
    flight.Sent(0, 9, 1000, started);
    const auto delayed = EncodeVideoFeedbackDatagram({.elapsed_us = 350000, .complete_frames = 7, .complete_bytes = 7000, .latest_frame_index = 7});
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
    ASSERT_TRUE(probe_->changed.wait_for(lock, std::chrono::seconds(2), [probe = probe_] { return !probe->feedback.empty(); }));
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

TEST_F(IrohMediaTest, CaptureAdmissionWaitsForReceiptWithoutBreakingTheEncodedReferenceChain) {
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
    EXPECT_FALSE(sender_->CanEncodeVideo(frame.stream));
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
