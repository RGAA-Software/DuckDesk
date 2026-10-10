#include "iroh_session.h"

#include "px_common/log.h"
#include "px_common/uuid.h"
#include "px_message.pb.h"
#include "px_render/modules/module_ids.h"

namespace px {
namespace {
ResourceChannelCloseOutcome CloseOutcome(rdp::BridgeCloseReason reason) {
    switch (reason) {
        case rdp::BridgeCloseReason::kPeerClosed:
            return ResourceChannelCloseOutcome::kPeerClosed;
        case rdp::BridgeCloseReason::kStopped:
            return ResourceChannelCloseOutcome::kUserStopped;
        case rdp::BridgeCloseReason::kInvalidPacket:
        case rdp::BridgeCloseReason::kQueueFull:
            return ResourceChannelCloseOutcome::kIoError;
        default:
            return ResourceChannelCloseOutcome::kTransportLost;
    }
}
}  // namespace

IrohSession::IrohSession(AcceptedIrohFrontend accepted, RenderEventCallback events) : accepted_(std::move(accepted)), events_(std::move(events)) {}

IrohSession::~IrohSession() { Close(); }

bool IrohSession::Start(asio::any_io_executor executor, std::uint16_t rdp_proxy_port) {
    const auto owner = weak_from_this();
    const rdp::StreamBinding binding{.connection_id = GenerateRandomBase64Id(), .generation = 1};
    std::shared_ptr<transport::MessageSession> messages{};
    std::shared_ptr<rdp::RdpTcpBridge> bridge{};
    {
        std::lock_guard lock(mutex_);
        if (closed_ || started_once_ || !events_ || !accepted_.frontend || accepted_.frontend->IsClosed() ||
            accepted_.frontend->IsRdp() != (rdp_proxy_port != 0))
            return false;
        started_once_ = true;
        transport::MessageSessionCallbacks callbacks{};
        callbacks.message = [owner](transport::ChannelKind kind, std::shared_ptr<Data> payload) {
            if (const auto session = owner.lock()) session->Receive(kind, std::move(payload));
        };
        if (rdp_proxy_port == 0) {
            callbacks.datagram = [owner](transport::Bytes payload) {
                const auto session = owner.lock();
                if (!session || !session->IsAlive() || !session->accepted_.frontend->Allows("audio")) return;
                if (const auto voice = transport::DecodeVoiceDatagram(payload))
                    session->Receive(transport::ChannelKind::kControl, Data::From(voice->SerializeAsString()), true);
            };
        }
        callbacks.closed = [owner] {
            if (const auto session = owner.lock()) session->Close(ResourceChannelCloseOutcome::kTransportLost);
        };
        messages_ = std::make_shared<transport::MessageSession>(accepted_.connection, accepted_.channels, std::move(callbacks));
        messages = messages_;
        if (rdp_proxy_port == 0) media_sender_ = std::make_shared<transport::MediaDatagramSender>(accepted_.connection, executor);
        if (rdp_proxy_port != 0) {
            rdp_bridge_ = rdp::RdpTcpBridge::Create(
                std::move(executor), binding,
                [owner](std::shared_ptr<Data> payload, rdp::RdpTcpBridge::SendCompletion completion) {
                    if (const auto session = owner.lock())
                        session->Send(std::move(payload), std::move(completion));
                    else
                        completion(false);
                },
                [owner](rdp::BridgeCloseReason reason) {
                    if (const auto session = owner.lock()) session->Close(CloseOutcome(reason));
                },
                [owner](std::size_t sent_bytes, std::size_t received_bytes) {
                    if (const auto session = owner.lock()) session->ReportTraffic(sent_bytes, received_bytes);
                });
            bridge = rdp_bridge_;
            if (!bridge) return false;
        }
        if (!messages->Start()) return false;
    }
    if (bridge) {
        Send(rdp::EncodeOpen(binding), [owner, weak_bridge = std::weak_ptr<rdp::RdpTcpBridge>{bridge}, rdp_proxy_port](bool delivered) {
            if (const auto active = weak_bridge.lock(); active && delivered)
                active->ConnectLoopback(rdp_proxy_port);
            else if (const auto session = owner.lock())
                session->Close(ResourceChannelCloseOutcome::kTransportLost);
        });
    }
    return messages->IsAlive() && !closed_;
}

void IrohSession::Close(ResourceChannelCloseOutcome outcome) {
    std::shared_ptr<transport::MessageSession> messages{};
    std::shared_ptr<rdp::RdpTcpBridge> bridge{};
    std::shared_ptr<transport::MediaDatagramSender> media_sender{};
    {
        std::lock_guard lock(mutex_);
        if (closed_.exchange(true)) return;
        messages = messages_;
        bridge = rdp_bridge_;
        media_sender = media_sender_;
    }
    if (bridge) bridge->Stop();
    if (media_sender) media_sender->Stop();
    if (accepted_.frontend) accepted_.frontend->Close(outcome);
    if (messages) messages->Stop();
}

void IrohSession::Send(std::shared_ptr<Data> payload, std::function<void(bool)> completion) {
    std::shared_ptr<transport::MessageSession> messages{};
    {
        std::lock_guard lock(mutex_);
        messages = messages_;
    }
    if (closed_ || !messages || !payload) {
        if (completion) completion(false);
        return;
    }
    const auto kind = transport::MessageChannel(payload);
    if ((kind == transport::ChannelKind::kFile && !accepted_.frontend->Allows("file")) ||
        (kind == transport::ChannelKind::kClipboard && !accepted_.frontend->Allows("clipboard")) ||
        (kind == transport::ChannelKind::kInput && !accepted_.frontend->Allows("input"))) {
        if (completion) completion(false);
        return;
    }
    const auto sent_bytes = payload->Size();
    messages->Send(std::move(payload), [owner = weak_from_this(), sent_bytes, completion = std::move(completion)](bool delivered) {
        if (const auto session = owner.lock(); session && delivered && !session->accepted_.frontend->IsRdp()) session->ReportTraffic(sent_bytes, 0);
        if (completion) completion(delivered);
    });
}

void IrohSession::UpdatePermissions(const std::vector<std::string>& permissions) {
    if (accepted_.frontend) accepted_.frontend->UpdatePermissions(permissions);
}

FileTransferSendResult IrohSession::SendFile(std::shared_ptr<Data> payload) {
    std::shared_ptr<transport::MessageSession> messages{};
    {
        std::lock_guard lock(mutex_);
        messages = messages_;
    }
    if (!IsAlive() || !messages || !accepted_.frontend->Allows("file")) return FileTransferSendResult::Disconnected("iroh file route is unavailable");
    const auto sent_bytes = payload ? payload->Size() : 0;
    return messages->SendFile(std::move(payload), [owner = weak_from_this(), sent_bytes](bool delivered) {
        if (const auto session = owner.lock(); session && delivered) session->ReportTraffic(sent_bytes, 0);
    });
}

bool IrohSession::SendDatagram(std::span<const std::uint8_t> payload) {
    if (closed_ || accepted_.frontend->IsRdp()) return false;
    const bool delivered = accepted_.connection->SendDatagram(payload).has_value();
    if (delivered) ReportTraffic(payload.size(), 0);
    return delivered;
}

bool IrohSession::IsAlive() const { return !closed_ && accepted_.frontend && !accepted_.frontend->IsClosed(); }

std::uint64_t IrohSession::VideoEncodingBitrate(std::uint64_t ceiling_bps) const {
    std::lock_guard lock(rate_mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_rate_sample_) {
        rate_snapshot_ = accepted_.connection->Snapshot();
        const auto offered_frames = offered_video_frames_.exchange(0);
        const auto dropped_frames = dropped_video_frames_.exchange(0);
        // Do not halve quality for a single scheduling hiccup. Sustained drops
        // (at least three and ten percent in this interval) signal overload.
        sampled_media_backpressure_ = dropped_frames >= 3 && dropped_frames * 10 >= offered_frames;
        next_rate_sample_ = now + std::chrono::milliseconds(500);
    }
    const bool receiver_stalled = delivery_progress_.Congested(now);
    const auto received_bps = delivery_progress_.ReceivedBitrate();
    const auto rate = rate_control_.Update(ceiling_bps, rate_snapshot_, now, sampled_media_backpressure_, receiver_stalled, received_bps);
    if (rate != last_rate_bps_) {
        LOGI("event=iroh.encoder_budget ceiling_bps={} video_bps={} rtt_us={} path={} media_backpressure={} receiver_stalled={} received_bps={}",
             ceiling_bps, rate, rate_snapshot_.rtt_us, static_cast<std::uint32_t>(rate_snapshot_.path), sampled_media_backpressure_, receiver_stalled,
             received_bps);
        last_rate_bps_ = rate;
    }
    return rate;
}

bool IrohSession::SendVideo(const media::VideoFrame& frame) {
    std::shared_ptr<transport::MediaDatagramSender> sender{};
    {
        std::lock_guard lock(mutex_);
        sender = media_sender_;
    }
    if (closed_ || !sender || !accepted_.frontend->Allows("view")) return false;
    ++offered_video_frames_;
    {
        std::lock_guard lock(rate_mutex_);
        delivery_progress_.Offered(frame.stream, frame.frame_index, std::chrono::steady_clock::now());
    }
    const bool accepted = sender->SendVideo(frame, [owner = weak_from_this()](bool delivered) {
        const auto session = owner.lock();
        if (!session || session->closed_ || delivered) return;
        ++session->dropped_video_frames_;
        // The receiver requests repair after complete-frame progress; enqueue failure is only a rate/backpressure signal.
    });
    if (!accepted) ++dropped_video_frames_;
    return accepted;
}

bool IrohSession::SendAudio(std::span<const std::uint8_t> opus) {
    std::shared_ptr<transport::MediaDatagramSender> sender{};
    {
        std::lock_guard lock(mutex_);
        sender = media_sender_;
    }
    return !closed_ && sender && accepted_.frontend->Allows("audio") && sender->SendAudio(opus);
}

bool IrohSession::SendVoice(const Message& message) {
    if (!IsAlive() || accepted_.frontend->IsRdp() || !accepted_.frontend->Allows("audio")) return false;
    const auto packet = transport::EncodeVoiceDatagram(message);
    return !packet.empty() && SendDatagram(packet);
}

void IrohSession::Receive(transport::ChannelKind kind, std::shared_ptr<Data> payload, bool voice_datagram) {
    if (closed_) return;
    Message envelope{};
    if (!payload || !envelope.ParseFromArray(payload->Bytes().data(), static_cast<int>(payload->Size()))) {
        Close(ResourceChannelCloseOutcome::kIoError);
        return;
    }
    if (accepted_.frontend->IsRdp()) {
        std::shared_ptr<rdp::RdpTcpBridge> bridge{};
        {
            std::lock_guard lock(mutex_);
            bridge = rdp_bridge_;
        }
        if (kind == transport::ChannelKind::kRdp && envelope.type() == kRdpStream && bridge && bridge->Receive(payload)) return;
        if (kind == transport::ChannelKind::kControl && envelope.type() == kHeartBeat && envelope.has_heartbeat()) {
            Message reply{};
            reply.set_type(kOnHeartBeat);
            reply.mutable_on_heartbeat()->set_timestamp(envelope.heartbeat().timestamp());
            Send(Data::From(reply.SerializeAsString()));
            return;
        }
        Close(ResourceChannelCloseOutcome::kIoError);
        return;
    }
    if ((kind == transport::ChannelKind::kInput && !accepted_.frontend->Allows("input")) ||
        (kind == transport::ChannelKind::kClipboard && !accepted_.frontend->Allows("clipboard")) ||
        (kind == transport::ChannelKind::kFile && !accepted_.frontend->Allows("file")))
        return;
    if (envelope.type() == kVoiceAudioFrame && !voice_datagram) return;
    if (envelope.type() == kMediaReceiveFeedback) {
        if (!envelope.has_media_receive_feedback() || !accepted_.frontend->Allows("view")) return;
        const auto& feedback = envelope.media_receive_feedback();
        if (feedback.media_stream() > 255) return;
        transport::VideoReceiveFeedback report{.stream = static_cast<std::uint8_t>(feedback.media_stream()),
                                               .elapsed_us = feedback.elapsed_us(),
                                               .complete_frames = feedback.complete_frames(),
                                               .complete_bytes = feedback.complete_bytes()};
        if (feedback.has_latest_frame_index()) report.latest_frame_index = feedback.latest_frame_index();
        std::lock_guard lock(rate_mutex_);
        delivery_progress_.Observe(report, std::chrono::steady_clock::now());
        return;
    }
    if (envelope.type() == kInsertKeyFrame) {
        if (accepted_.frontend->Allows("view")) RequestKeyFrame("");
        return;
    }
    if (envelope.type() == kMediaRecoveryRequest) {
        if (!envelope.has_media_recovery_request() || !accepted_.frontend->Allows("view")) return;
        const auto& recovery = envelope.media_recovery_request();
        if (recovery.media_stream() > 255 || recovery.monitor().size() > 255) return;
        if (recovery.action() == MediaRecoveryRequest::KEY_FRAME) {
            RequestKeyFrame(recovery.monitor());
        } else if (recovery.action() == MediaRecoveryRequest::INVALIDATE_REFERENCES && recovery.has_invalid_reference_frame()) {
            // RFI can fall back to IDR when the encoder reference window has expired.
            // Share the same limit with explicit IDR and decoder recovery.
            if (!AllowVideoRecovery(recovery.monitor())) return;
            const auto request = std::make_shared<ReferenceFrameInvalidationEvent>();
            request->monitor_name_ = recovery.monitor();
            request->invalid_frame_index_ = recovery.invalid_reference_frame();
            Publish(request);
        }
        return;
    }
    // Never let the message body select another admitted application's route.
    envelope.set_stream_id(StreamId());
    const auto event = std::make_shared<NetworkClientEvent>();
    event->message_ = Data::From(envelope.SerializeAsString());
    event->transport_type_ = TransportKind::kIroh;
    event->channel_type_ = voice_datagram                          ? TransportChannel::kMedia
                           : kind == transport::ChannelKind::kFile ? TransportChannel::kFileTransfer
                                                                   : TransportChannel::kReliableControl;
    event->connection_instance_id_ = BindingId();
    event->resource_connection_id_ = BindingId();
    ReportTraffic(0, payload->Size());
    Publish(event);
}

bool IrohSession::AllowVideoRecovery(const std::string& monitor) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);
    if (closed_) return false;
    for (const auto& [requested_monitor, requested_at] : video_recovery_requests_) {
        if ((monitor.empty() || requested_monitor.empty()) && now - requested_at < std::chrono::milliseconds(500)) return false;
    }
    const auto previous = video_recovery_requests_.find(monitor);
    if (previous != video_recovery_requests_.end() && now - previous->second < std::chrono::milliseconds(500)) return false;
    // Monitor names arrive over the control channel; bound per-session tracking.
    if (previous == video_recovery_requests_.end() && video_recovery_requests_.size() >= 32) return false;
    video_recovery_requests_[monitor] = now;
    return true;
}

void IrohSession::RequestKeyFrame(const std::string& monitor) {
    if (!AllowVideoRecovery(monitor)) return;
    // Decoder recovery and assembly recovery use the same event and admission limit.
    const auto request = std::make_shared<KeyFrameRequestEvent>();
    request->monitor_name_ = monitor;
    Publish(request);
}

void IrohSession::ReportTraffic(std::size_t sent_bytes, std::size_t received_bytes) {
    if (closed_ || (sent_bytes == 0 && received_bytes == 0)) return;
    const auto event = std::make_shared<ResourceTrafficEvent>();
    event->connection_id_ = BindingId();
    event->sent_bytes_ = sent_bytes;
    event->received_bytes_ = received_bytes;
    Publish(event);
}

void IrohSession::Publish(RenderEvent event) { events_(RenderEventEnvelope{.source_id = kNetIrohTransportId, .payload = std::move(event)}); }
}  // namespace px
