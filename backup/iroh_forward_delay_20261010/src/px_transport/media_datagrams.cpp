#include "media_datagrams.h"

#include "compact_audio_datagram.h"
#include "media_transport/packet_timing.h"
#include "paced_media_batch.h"
#include "px_common/log.h"
#include "px_common/thread.h"

namespace px::transport {
MediaDatagramSender::MediaDatagramSender(std::shared_ptr<Connection> connection, asio::any_io_executor executor, MediaSendOptions options)
    : connection_(std::move(connection)), executor_(std::move(executor)), options_(options) {}

MediaDatagramSender::~MediaDatagramSender() { Stop(); }

bool MediaDatagramSender::SendVideo(const media::VideoFrame& frame, std::function<void(bool)> completion) {
    if (stopped_ || !connection_ || connection_->DatagramLimit() < kMediaDatagramBytes || options_.frame_deadline.count() <= 0) return false;
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + options_.frame_deadline * (frame.kind == media::VideoFrameKind::kIdr ? 2 : 1);
    {
        std::lock_guard lock(video_mutex_);
        if (stopped_) return false;
        const auto [previous, first_frame] = last_encoded_frame_.try_emplace(frame.stream, started);
        const auto gap_ms = std::chrono::duration_cast<std::chrono::milliseconds>(started - previous->second).count();
        if (first_frame || gap_ms >= 100) {
            LOGI("event=iroh.encoded_frame phase={} stream={} frame={} gap_ms={} bytes={}", first_frame ? "first" : "gap", frame.stream,
                 frame.frame_index, gap_ms, frame.encoded.size());
        }
        previous->second = started;
        if (started >= next_path_sample_) {
            const auto snapshot = connection_->Snapshot();
            current_path_ = snapshot.path;
            flight_.ObserveRtt(snapshot.rtt_us);
            next_path_sample_ = started + std::chrono::milliseconds(500);
        }
        if (window_started_ == std::chrono::steady_clock::time_point{}) window_started_ = started;
        if (started - window_started_ >= std::chrono::seconds(5)) {
            const auto snapshot = connection_->Snapshot();
            const auto window_us = std::chrono::duration_cast<std::chrono::microseconds>(started - window_started_).count();
            LOGI("event=iroh.media_budget encoded_bps={} video_datagram_bps={} opus_bps={} audio_datagram_bps={} flight_bytes={} flight_limited={}",
                 encoded_bytes_ * 8000000 / window_us, video_datagram_bytes_.exchange(0) * 8000000 / window_us,
                 opus_bytes_.exchange(0) * 8000000 / window_us, audio_datagram_bytes_.exchange(0) * 8000000 / window_us, flight_.PendingBytes(),
                 flight_limited_frames_);
            LOGI("event=iroh.send_window offered={} idr={} busy={} completed={} failed={} max_send_us={} path={} rtt_us={} tx_packets={} tx_lost={}",
                 offered_frames_, offered_idr_frames_, busy_frames_, completed_frames_.exchange(0), failed_frames_.exchange(0),
                 maximum_send_us_.exchange(0), static_cast<std::uint32_t>(snapshot.path), snapshot.rtt_us, snapshot.sent_packets,
                 snapshot.lost_packets);
            window_started_ = started;
            offered_frames_ = 0;
            offered_idr_frames_ = 0;
            busy_frames_ = 0;
            encoded_bytes_ = 0;
            flight_limited_frames_ = 0;
        }
        ++offered_frames_;
        encoded_bytes_ += frame.encoded.size();
        if (frame.kind == media::VideoFrameKind::kIdr) ++offered_idr_frames_;
        auto& sequence = sequences_[frame.stream];
        // Count offered frames, including ones dropped by backpressure, so reference gaps remain visible.
        const auto transport_frame = sequence.frame++;
        constexpr std::size_t kMaximumPendingFrames{4};
        constexpr std::size_t kMaximumPendingBytes{4 * 1024 * 1024};
        if (pending_video_.size() + (video_active_ ? 1 : 0) >= kMaximumPendingFrames ||
            frame.encoded.size() > kMaximumPendingBytes - pending_video_bytes_) {
            ++busy_frames_;
            return false;
        }
        auto packets = media::PacketizeVideoFrame(frame, {.frame_index = transport_frame,
                                                          .timestamp_90khz = static_cast<std::uint32_t>(media::MediaSteadyMicros() * 9 / 100),
                                                          .sequence = sequence.packet,
                                                          .datagram_size = kMediaDatagramBytes,
                                                          // Relay already carries QUIC packets over reliable TCP.
                                                          // Extra parity mostly adds queue pressure there; keep it for direct UDP.
                                                          .fec_percent = current_path_ == PathKind::kRelay ? std::uint8_t{0} : options_.fec_percent,
                                                          .minimum_parity = 1});
        if (!packets) return false;
        std::size_t packet_bytes{};
        for (const auto& packet : packets->packets) packet_bytes += packet.size();
        if (packet_bytes > kMaximumPendingBytes - pending_video_bytes_) {
            ++busy_frames_;
            return false;
        }
        // Flight credit controls capture admission, never the already encoded
        // prediction chain. A variable-size frame may cross the soft flight
        // budget; account it fully and pause subsequent capture until receipts
        // free capacity. The hard local queue/byte limits above remain bounded.
        flight_.Sent(frame.stream, frame.frame_index, packet_bytes, started);
        sequence.packet = packets->next_sequence;
        pending_video_bytes_ += packet_bytes;
        pending_video_.push_back({std::move(packets->packets), started, deadline, packet_bytes, std::move(completion)});
    }
    StartNextVideo();
    return true;
}

void MediaDatagramSender::StartNextVideo() {
    PendingVideo pending{};
    {
        std::lock_guard lock(video_mutex_);
        if (stopped_ || video_active_ || pending_video_.empty()) return;
        pending = std::move(pending_video_.front());
        pending_video_.pop_front();
        video_active_ = true;
    }
    const auto owner = weak_from_this();
    const auto batch = std::make_shared<PacedMediaBatch>(
        // QUIC owns packet pacing; the frame queue only bounds latency and memory.
        executor_, std::move(pending.packets), true,
        [owner, deadline = pending.deadline](std::span<const media::Packet> burst) {
            const auto sender = owner.lock();
            if (!sender || sender->stopped_) return false;
            for (const auto& packet : burst) {
                const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                if (sender->stopped_ || remaining.count() <= 0 ||
                    !sender->connection_->SendDatagram(packet, static_cast<std::uint32_t>(remaining.count())))
                    return false;
                sender->video_datagram_bytes_ += packet.size();
            }
            return true;
        },
        [owner, deadline = pending.deadline] {
            const auto sender = owner.lock();
            return sender && !sender->stopped_ && !sender->connection_->IsClosed() && std::chrono::steady_clock::now() < deadline;
        },
        [owner, started = pending.started, packet_bytes = pending.bytes, completion = std::move(pending.completion)](bool delivered) {
            const auto sender = owner.lock();
            if (sender) {
                if (delivered)
                    ++sender->completed_frames_;
                else
                    ++sender->failed_frames_;
                const auto elapsed_us = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
                auto maximum_us = sender->maximum_send_us_.load();
                while (maximum_us < elapsed_us && !sender->maximum_send_us_.compare_exchange_weak(maximum_us, elapsed_us)) {
                }
                std::lock_guard lock(sender->video_mutex_);
                sender->pending_video_bytes_ -= packet_bytes;
                sender->video_active_ = false;
            }
            if (completion) completion(delivered);
            if (sender) sender->StartNextVideo();
        });
    asio::post(executor_, [batch] { batch->Start(); });
}

bool MediaDatagramSender::SendAudio(std::span<const std::uint8_t> opus) {
    if (stopped_ || !connection_ || connection_->DatagramLimit() < kMediaDatagramBytes) return false;
    // Keep each audio/FEC block's packet order without queuing audio behind video.
    std::lock_guard lock(audio_mutex_);
    const auto packets = audio_.Push(opus, kMediaDatagramBytes);
    if (packets.empty()) return false;
    opus_bytes_ += opus.size();
    for (const auto& packet : packets) {
        const auto compact = CompactAudioDatagram(packet);
        if (stopped_ || compact.empty() || !connection_->SendDatagram(compact)) return false;
        audio_datagram_bytes_ += compact.size();
    }
    return true;
}

void MediaDatagramSender::ObserveFeedback(const VideoReceiveFeedback& report) {
    std::lock_guard lock(video_mutex_);
    if (stopped_) return;
    const auto now = std::chrono::steady_clock::now();
    auto& trace = feedback_traces_[report.stream];
    const auto arrival_gap_us = trace.arrived ? std::chrono::duration_cast<std::chrono::microseconds>(now - *trace.arrived).count() : 0;
    const auto generated_gap_us = report.elapsed_us >= trace.elapsed_us ? report.elapsed_us - trace.elapsed_us : 0;
    const auto before = flight_.Inspect(report.stream, now);
    flight_.Observe(report, now);
    const auto after = flight_.Inspect(report.stream, now);
    if (arrival_gap_us >= 100000 || (trace.block_logged && before.frames != after.frames)) {
        LOGI("event=iroh.feedback_trace stream={} frame={} arrival_gap_us={} generated_gap_us={} released={} pending={} bytes={} oldest={} age_us={}",
             report.stream, report.latest_frame_index.value_or(0), arrival_gap_us, generated_gap_us, before.frames - after.frames, after.frames,
             after.bytes, after.oldest_frame, after.oldest_age_us);
    }
    trace.arrived = now;
    trace.elapsed_us = std::max(trace.elapsed_us, report.elapsed_us);
}

bool MediaDatagramSender::CanEncodeVideo(std::uint8_t stream) {
    std::lock_guard lock(video_mutex_);
    if (stopped_) return false;
    const auto now = std::chrono::steady_clock::now();
    const auto queued_frames = pending_video_.size() + (video_active_ ? 1 : 0);
    const bool ready = queued_frames < 4 && flight_.CanSend(stream, 0, now);
    auto& trace = feedback_traces_[stream];
    if (!ready && !trace.blocked_since) trace.blocked_since = now;
    const auto blocked_us = trace.blocked_since ? std::chrono::duration_cast<std::chrono::microseconds>(now - *trace.blocked_since).count() : 0;
    if (blocked_us >= 80000 && (ready || !trace.block_logged)) {
        const auto snapshot = flight_.Inspect(stream, now);
        const auto feedback_age_us = trace.arrived ? std::chrono::duration_cast<std::chrono::microseconds>(now - *trace.arrived).count() : -1;
        LOGI(
            "event=iroh.capture_credit phase={} stream={} blocked_us={} queued={} pending={} bytes={} oldest={} newest={} age_us={} probe_us={} "
            "feedback_age_us={}",
            ready ? "resume" : "blocked", stream, blocked_us, queued_frames, snapshot.frames, snapshot.bytes, snapshot.oldest_frame,
            snapshot.newest_frame, snapshot.oldest_age_us, snapshot.probe_wait_us, feedback_age_us);
        trace.block_logged = true;
        const auto path = connection_->Snapshot();
        LOGI("event=iroh.capture_transport phase={} stream={} rtt_us={} cwnd_bytes={} datagram_space={} congestion_events={} progress_age_us={} budget_bytes={}",
             ready ? "resume" : "blocked", stream, path.rtt_us, path.congestion_window_bytes, path.datagram_buffer_space, path.congestion_events,
             snapshot.progress_age_us, snapshot.budget_bytes);
    }
    if (ready) {
        trace.blocked_since.reset();
        trace.block_logged = false;
    }
    return ready;
}

void MediaDatagramSender::Stop() {
    std::deque<PendingVideo> cancelled{};
    {
        std::lock_guard lock(video_mutex_);
        stopped_ = true;
        cancelled.swap(pending_video_);
        for (const auto& pending : cancelled) pending_video_bytes_ -= pending.bytes;
    }
    for (auto& pending : cancelled) {
        if (pending.completion) pending.completion(false);
    }
}

MediaDatagramReceiver::MediaDatagramReceiver(std::shared_ptr<Connection> connection, MediaReceiveCallbacks callbacks)
    : connection_(std::move(connection)), callbacks_(std::move(callbacks)) {}

MediaDatagramReceiver::~MediaDatagramReceiver() { Stop(); }

bool MediaDatagramReceiver::Start() {
    std::lock_guard lock(mutex_);
    if (!connection_ || connection_->IsClosed() || started_once_) return false;
    started_once_ = true;
    stopped_ = false;
    worker_ = Thread::MakeOnceTask([owner = weak_from_this(), connection = connection_] { Receive(owner, connection); }, "iroh-media-receive");
    return true;
}

void MediaDatagramReceiver::Stop() {
    std::shared_ptr<Thread> worker{};
    {
        std::lock_guard lock(mutex_);
        if (stopped_.exchange(true)) return;
        worker = std::move(worker_);
    }
    // The bounded receive timeout makes media-only stop independent of the reliable connection lifetime.
    if (worker) worker->Exit();
}

void MediaDatagramReceiver::Receive(std::weak_ptr<MediaDatagramReceiver> owner, std::shared_ptr<Connection> connection) {
    for (;;) {
        const auto receive_started_us = media::MediaSteadyMicros();
        const auto packet = connection->ReceiveDatagram(10);
        const auto received_us = media::MediaSteadyMicros();
        const auto receiver = owner.lock();
        if (!receiver || receiver->stopped_) return;
        if (packet) {
            receiver->Feed(*packet, received_us);
        } else if (packet.error() != Error::kTimeout) {
            receiver->Stop();
            if (receiver->callbacks_.closed) receiver->callbacks_.closed();
            return;
        }
        if (!receiver->stopped_) receiver->PollRecovery(media::MediaSteadyMicros());
        const auto processing_us = media::MediaSteadyMicros() - received_us;
        if (processing_us >= 20000 || received_us - receive_started_us >= 50000) {
            LOGI("event=iroh.receive_worker wait_us={} processing_us={} has_packet={}", received_us - receive_started_us, processing_us,
                 packet.has_value());
        }
    }
}

void MediaDatagramReceiver::Feed(std::span<const std::uint8_t> payload, std::uint64_t now_us) {
    if (const auto voice = DecodeVoiceDatagram(payload)) {
        if (callbacks_.voice) callbacks_.voice(voice);
        return;
    }
    const auto expanded_audio = ExpandAudioDatagram(payload);
    const auto datagram = media::ParseMedia(expanded_audio ? std::span<const std::uint8_t>{*expanded_audio} : payload);
    if (!datagram) return;
    if (datagram->kind == media::MediaKind::kVideo) {
        auto received = video_.Feed(*datagram, now_us);
        const auto assembled_us = media::MediaSteadyMicros();
        if (received.completed_frame_index) {
            const auto timing = transit_timing_[datagram->stream].Observe(received.completed_timestamp_90khz, now_us);
            auto& logged_us = transit_logged_us_[datagram->stream];
            if ((timing.receive_gap_us >= 80000 || timing.excess_transit_us >= 80000) && now_us - logged_us >= 100000) {
                LOGI("event=iroh.frame_transit stream={} frame={} source_gap_us={} receive_gap_us={} excess_us={} assembly_call_us={}",
                     datagram->stream, *received.completed_frame_index, timing.source_gap_us, timing.receive_gap_us, timing.excess_transit_us,
                     assembled_us - now_us);
                logged_us = now_us;
            }
        }
        receive_progress_.Observe(datagram->stream, received, now_us);
        auto& logged_us = statistics_logged_us_[datagram->stream];
        if (received.statistics && now_us - logged_us >= 5000000) {
            const auto& statistics = *received.statistics;
            LOGI("event=iroh.reassembly stream={} data={} parity={} duplicate={} late={} reordered={} recovered={} complete={} loss={} malformed={}",
                 datagram->stream, statistics.data_packets, statistics.parity_packets, statistics.duplicates, statistics.late_packets,
                 statistics.reordered_packets, statistics.recovered_data, statistics.completed_frames, statistics.final_loss_events,
                 statistics.malformed_packets);
            logged_us = now_us;
        }
        if (const auto request = recovery_.Observe(datagram->stream, received, now_us); request && callbacks_.recovery) callbacks_.recovery(*request);
        if (!stopped_ && received.frame && callbacks_.video) callbacks_.video(std::move(*received.frame));
    } else if (datagram->kind == media::MediaKind::kAudio) {
        auto received = audio_.Feed(datagram->payload, now_us);
        for (auto& packet : received.packets) {
            if (stopped_) return;
            if (callbacks_.audio) callbacks_.audio(std::move(packet));
        }
    }
}

void MediaDatagramReceiver::PollRecovery(std::uint64_t now_us) {
    for (const auto& feedback : receive_progress_.Poll(now_us)) {
        if (stopped_) return;
        auto& previous_elapsed = feedback_elapsed_us_[feedback.stream];
        if (previous_elapsed && feedback.elapsed_us - previous_elapsed >= 100000) {
            LOGI("event=iroh.feedback_generated stream={} frame={} gap_us={}", feedback.stream, feedback.latest_frame_index.value_or(0),
                 feedback.elapsed_us - previous_elapsed);
        }
        previous_elapsed = feedback.elapsed_us;
        if (callbacks_.feedback) callbacks_.feedback(feedback);
    }
    for (auto& request : recovery_.PollDue(now_us)) {
        if (stopped_) return;
        if (callbacks_.recovery) callbacks_.recovery(std::move(request));
    }
}
}  // namespace px::transport
