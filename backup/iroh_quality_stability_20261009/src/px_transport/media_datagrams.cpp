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
        if (!flight_.CanSend(frame.stream, packet_bytes, started)) {
            ++busy_frames_;
            ++flight_limited_frames_;
            return false;
        }
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
    if (!stopped_) flight_.Observe(report);
}

bool MediaDatagramSender::CanEncodeVideo(std::uint8_t stream) {
    std::lock_guard lock(video_mutex_);
    return !stopped_ && pending_video_.size() + (video_active_ ? 1 : 0) < 4 && flight_.CanSend(stream, 0, std::chrono::steady_clock::now());
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
        const auto packet = connection->ReceiveDatagram(10);
        const auto receiver = owner.lock();
        if (!receiver || receiver->stopped_) return;
        if (packet) {
            receiver->Feed(*packet, media::MediaSteadyMicros());
        } else if (packet.error() != Error::kTimeout) {
            receiver->Stop();
            if (receiver->callbacks_.closed) receiver->callbacks_.closed();
            return;
        }
        if (!receiver->stopped_) receiver->PollRecovery(media::MediaSteadyMicros());
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
        if (callbacks_.feedback) callbacks_.feedback(feedback);
    }
    for (auto& request : recovery_.PollDue(now_us)) {
        if (stopped_) return;
        if (callbacks_.recovery) callbacks_.recovery(std::move(request));
    }
}
}  // namespace px::transport
