#include "iroh_connection.h"

#include "media_transport/packet_timing.h"
#include "px_common/data.h"
#include "px_common/log.h"

namespace px {
IrohConnection::IrohConnection(const std::shared_ptr<MessageNotifier>& notifier, std::shared_ptr<transport::Connection> connection,
                               std::shared_ptr<transport::Channel> channel)
    : IrohConnection(notifier, std::move(connection), transport::SessionChannels{{transport::ChannelKind::kControl, std::move(channel)}}) {}

IrohConnection::IrohConnection(const std::shared_ptr<MessageNotifier>& notifier, std::shared_ptr<transport::Connection> connection,
                               transport::SessionChannels channels)
    : Connection(notifier), connection_(std::move(connection)), channels_(std::move(channels)) {}

IrohConnection::~IrohConnection() { Stop(); }

void IrohConnection::ObserveVideo(const media::VideoFrame& frame) {
    const auto now_us = media::MediaSteadyMicros();
    auto& window = receive_windows_[frame.stream];
    if (!window.started_us) window.started_us = now_us;
    if (window.previous_frame_us) {
        const auto gap_us = now_us - *window.previous_frame_us;
        window.maximum_gap_us = std::max(window.maximum_gap_us, gap_us);
        if (gap_us > 100000) ++window.gaps_over_100ms;
    }
    window.previous_frame_us = now_us;
    ++window.frames;
    const auto elapsed_us = now_us - *window.started_us;
    if (elapsed_us < 5000000) return;
    const auto snapshot = connection_->Snapshot();
    const auto path_name = snapshot.path == transport::PathKind::kDirect  ? "direct"
                           : snapshot.path == transport::PathKind::kRelay ? "relay"
                                                                          : "unknown";
    LOGI("event=iroh.path path={} open_paths={} rtt_us={} tx_packets={} rx_packets={} tx_lost={} tx_datagrams={} rx_datagrams={}", path_name,
         snapshot.open_paths, snapshot.rtt_us, snapshot.sent_packets, snapshot.received_packets, snapshot.lost_packets, snapshot.sent_datagrams,
         snapshot.received_datagrams);
    LOGI("iroh media receive window: stream={}, frames={}, fps={:.1f}, max_gap_ms={:.1f}, gaps_gt_100ms={}, idr={}, rfi={}", frame.stream,
         window.frames, 1000000.0 * window.frames / elapsed_us, window.maximum_gap_us / 1000.0, window.gaps_over_100ms, window.key_frame_requests,
         window.reference_requests);
    window = {.started_us = now_us, .previous_frame_us = now_us};
}

void IrohConnection::Start() {
    bool ready{};
    std::shared_ptr<transport::MessageSession> session{};
    {
        std::lock_guard lock(mutex_);
        if (started_once_ || stop_requested_) return;
        started_once_ = true;
        const auto owner = weak_from_this();
        transport::MessageSessionCallbacks callbacks{};
        callbacks.message = [owner](transport::ChannelKind, std::shared_ptr<Data> payload) {
            if (const auto adapter = owner.lock(); adapter && adapter->msg_cbk_) adapter->msg_cbk_(std::move(payload));
        };
        callbacks.closed = [owner] {
            if (const auto adapter = owner.lock()) {
                adapter->NotifyFileTransferClosed();
                if (adapter->dis_conn_cbk_) adapter->dis_conn_cbk_();
            }
        };
        callbacks.writable = [owner] {
            if (const auto adapter = owner.lock()) adapter->NotifyFileTransferWritable();
        };
        const bool receives_media = video_callback_ || audio_callback_ || voice_callback_;
        if (!receives_media) callbacks.datagram = datagram_callback_;
        session_ = std::make_shared<transport::MessageSession>(connection_, std::move(channels_), std::move(callbacks));
        session = session_;
        ready = session->Start();
        if (ready && receives_media) {
            transport::MediaReceiveCallbacks media_callbacks{};
            media_callbacks.voice = [owner](std::shared_ptr<Message> message) {
                if (const auto adapter = owner.lock(); adapter && !adapter->stop_requested_ && adapter->voice_callback_)
                    adapter->voice_callback_(std::move(message));
            };
            media_callbacks.video = [owner](media::VideoFrame frame) {
                if (const auto adapter = owner.lock(); adapter && adapter->video_callback_) {
                    adapter->ObserveVideo(frame);
                    adapter->video_callback_(MakeReassembledVideoDelivery(frame));
                }
            };
            media_callbacks.audio = [owner](media::AudioDelivery frame) {
                const auto adapter = owner.lock();
                if (!adapter || !adapter->audio_callback_) return;
                const auto opus = media::UnwrapAudioPayload(frame.payload);
                const auto message = std::make_shared<Message>();
                message->set_type(kAudioFrame);
                auto& audio = *message->mutable_audio_frame();
                audio.set_samples(48000);
                audio.set_channels(2);
                audio.set_bits(16);
                audio.set_frame_size(960);
                if (opus) audio.set_data(opus->data(), opus->size());
                adapter->audio_callback_(message);
            };
            media_callbacks.recovery = [owner](media::VideoRecoveryRequest request) {
                const auto adapter = owner.lock();
                if (!adapter) return;
                auto& window = adapter->receive_windows_[request.stream];
                if (request.kind == media::VideoRecoveryRequestKind::kKeyFrame)
                    ++window.key_frame_requests;
                else
                    ++window.reference_requests;
                Message message{};
                message.set_type(kMediaRecoveryRequest);
                auto& recovery = *message.mutable_media_recovery_request();
                recovery.set_action(request.kind == media::VideoRecoveryRequestKind::kKeyFrame ? MediaRecoveryRequest::KEY_FRAME
                                                                                               : MediaRecoveryRequest::INVALIDATE_REFERENCES);
                recovery.set_monitor(request.monitor);
                recovery.set_media_stream(request.stream);
                if (request.invalid_reference_frame) recovery.set_invalid_reference_frame(*request.invalid_reference_frame);
                adapter->PostBinaryMessage(Data::From(message.SerializeAsString()));
            };
            media_receiver_ = std::make_shared<transport::MediaDatagramReceiver>(connection_, std::move(media_callbacks));
            ready = media_receiver_->Start();
        }
    }
    if (!ready) {
        Stop();
        if (dis_conn_cbk_) dis_conn_cbk_();
        return;
    }
    if (!stop_requested_ && session->IsAlive() && conn_cbk_) conn_cbk_();
}

void IrohConnection::Stop() {
    if (stop_requested_.exchange(true)) return;
    std::shared_ptr<transport::MessageSession> session{};
    std::shared_ptr<transport::MediaDatagramReceiver> receiver{};
    {
        std::lock_guard lock(mutex_);
        session = session_;
        receiver = media_receiver_;
    }
    if (session) session->Stop();
    if (connection_) connection_->Close();
    if (receiver) receiver->Stop();
    NotifyFileTransferClosed();
}

void IrohConnection::PostBinaryMessage(std::shared_ptr<Data> payload) { PostReliableBinaryMessage(std::move(payload), {}); }

void IrohConnection::PostReliableBinaryMessage(std::shared_ptr<Data> payload, std::function<void(bool)> completion) {
    std::shared_ptr<transport::MessageSession> session{};
    {
        std::lock_guard lock(mutex_);
        session = session_;
    }
    if (!session) {
        if (completion) completion(false);
        return;
    }
    ++queuing_message_count_;
    session->Send(std::move(payload), [owner = weak_from_this(), completion = std::move(completion)](bool delivered) {
        if (const auto adapter = owner.lock()) --adapter->queuing_message_count_;
        if (completion) completion(delivered);
    });
}

FileTransferSendResult IrohConnection::PostFileTransferMessage(std::shared_ptr<Data> payload) {
    std::shared_ptr<transport::MessageSession> session{};
    {
        std::lock_guard lock(mutex_);
        session = session_;
    }
    if (!session) return FileTransferSendResult::Disconnected("iroh session is not ready");
    ++queuing_message_count_;
    return session->SendFile(std::move(payload), [owner = weak_from_this()](bool) {
        if (const auto adapter = owner.lock()) --adapter->queuing_message_count_;
    });
}

bool IrohConnection::IsAlive() {
    std::lock_guard lock(mutex_);
    return session_ && session_->IsAlive();
}

void IrohConnection::SetDatagramCallback(std::function<void(transport::Bytes)> callback) {
    std::lock_guard lock(mutex_);
    if (!started_once_) datagram_callback_ = std::move(callback);
}

void IrohConnection::SetMediaCallbacks(std::function<void(EncodedVideoDelivery)> video, std::function<void(std::shared_ptr<Message>)> audio) {
    std::lock_guard lock(mutex_);
    if (started_once_ || stop_requested_) return;
    video_callback_ = std::move(video);
    audio_callback_ = std::move(audio);
}

bool IrohConnection::SendDatagram(std::span<const std::uint8_t> payload) {
    std::shared_ptr<transport::MessageSession> session{};
    {
        std::lock_guard lock(mutex_);
        session = session_;
    }
    return session && session->SendDatagram(payload);
}

bool IrohConnection::SendVoice(const Message& message) {
    const auto packet = transport::EncodeVoiceDatagram(message);
    return !packet.empty() && SendDatagram(packet);
}

void IrohConnection::SetVoiceCallback(std::function<void(std::shared_ptr<Message>)> voice) {
    std::lock_guard lock(mutex_);
    if (!started_once_ && !stop_requested_) voice_callback_ = std::move(voice);
}
}  // namespace px
