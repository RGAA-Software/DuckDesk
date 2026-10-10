#include "iroh_transport.h"

#include <nlohmann/json.hpp>

#include "media_transport/send_policy.h"
#include "px_common/data.h"
#include "px_message.pb.h"
#include "px_render/modules/module_ids.h"

namespace px {
IrohTransport::IrohTransport(std::shared_ptr<WsTransport> services, std::shared_ptr<PxAsyncRuntime> runtime)
    : services_(std::move(services)), runtime_(std::move(runtime)) {}
IrohTransport::~IrohTransport() { Stop(); }
std::string IrohTransport::Id() const { return kNetIrohTransportId; }

bool IrohTransport::Start(const RenderModuleConfiguration& configuration) {
    if (!runtime_ || !services_ || configuration.iroh_endpoint_configuration.empty() ||
        (configuration.app_mode == "rdp") != (configuration.rdp_proxy_port != 0))
        return false;
    std::lock_guard lock(mutex_);
    if (started_once_ || !RenderModule::Start(configuration)) return false;
    started_once_ = true;
    const auto owner = std::weak_ptr<IrohTransport>{std::static_pointer_cast<IrohTransport>(shared_from_this())};
    server_ = std::make_shared<IrohServer>(
        services_, runtime_, configuration.app_mode == "rdp",
        [owner](AcceptedIrohFrontend accepted) {
            const auto transport = owner.lock();
            return transport && transport->Accept(std::move(accepted));
        },
        MakeImmediateEventDispatcher());
    auto endpoint_configuration = nlohmann::json::parse(configuration.iroh_endpoint_configuration, nullptr, false);
    if (!endpoint_configuration.is_object() || configuration.udp_listen_port < 0 || configuration.udp_listen_port > 65535) return false;
    endpoint_configuration["bind_address"] = "0.0.0.0:" + std::to_string(configuration.udp_listen_port);
    running_ = server_->Start(endpoint_configuration.dump());
    return running_;
}

bool IrohTransport::Stop() {
    std::shared_ptr<IrohServer> server{};
    std::vector<std::shared_ptr<IrohSession>> sessions{};
    {
        std::lock_guard lock(mutex_);
        running_ = false;
        server = std::move(server_);
        sessions.swap(sessions_);
    }
    // Release business occupancy while event dispatch is still active.
    for (const auto& session : sessions) session->Close();
    if (server) server->Stop();
    return RenderModule::Stop();
}
bool IrohTransport::Destroy() {
    Stop();
    return RenderModule::Destroy();
}
bool IrohTransport::IsWorking() const { return running_ && IsEnabled(); }

std::expected<std::string, transport::Error> IrohTransport::Address() const {
    std::lock_guard lock(mutex_);
    if (!running_ || !server_) return std::unexpected(transport::Error::kClosed);
    return server_->Address();
}

bool IrohTransport::Accept(AcceptedIrohFrontend accepted) {
    const auto session = std::make_shared<IrohSession>(std::move(accepted), MakeImmediateEventDispatcher());
    if (!session->Start(runtime_->Executor(PxAsyncLane::kWorker), configuration_.rdp_proxy_port)) return false;
    std::lock_guard lock(mutex_);
    if (!running_ || !IsEnabled()) return false;
    sessions_.push_back(session);
    return true;
}

std::vector<std::shared_ptr<IrohSession>> IrohTransport::Sessions() const {
    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<IrohSession>> active{};
    if (!IsWorking()) return active;
    for (const auto& session : sessions_)
        if (session->IsAlive()) active.push_back(session);
    return active;
}
void IrohTransport::Tick1Second() {
    std::vector<std::shared_ptr<IrohSession>> expired{};
    {
        std::lock_guard lock(mutex_);
        for (auto session = sessions_.begin(); session != sessions_.end();) {
            if ((*session)->IsAlive()) {
                ++session;
                continue;
            }
            expired.push_back(std::move(*session));
            session = sessions_.erase(session);
        }
    }
    for (const auto& session : expired) session->Close();
}
int IrohTransport::ConnectedClientCount() const { return static_cast<int>(Sessions().size()); }
std::uint64_t IrohTransport::VideoEncodingBitrate(std::uint64_t requested_bps) const {
    const auto ceiling =
        media::SendBudget::FromTotal(requested_bps, transport::MediaSendOptions{}.fec_percent, transport::kMediaDatagramBytes).video_bps;
    auto budget = ceiling;
    for (const auto& session : Sessions()) {
        if (session->HasVideo()) budget = std::min(budget, session->VideoEncodingBitrate(ceiling));
    }
    return budget;
}
bool IrohTransport::HasVideoClient() const {
    for (const auto& session : Sessions())
        if (session->HasVideo()) return true;
    return false;
}
bool IrohTransport::SendToSession(const std::shared_ptr<IrohSession>& session, const std::shared_ptr<Data>& message) {
    Message envelope{};
    if (!message || !envelope.ParseFromArray(message->Bytes().data(), static_cast<int>(message->Size()))) return false;
    if (envelope.type() == kAudioFrame && envelope.has_audio_frame()) {
        const auto& opus = envelope.audio_frame().data();
        return session->SendAudio({reinterpret_cast<const std::uint8_t*>(opus.data()), opus.size()});
    } else if (envelope.type() == kVoiceAudioFrame) {
        return session->SendVoice(envelope);
    } else if (envelope.type() != kVideoFrame) {
        session->Send(message);
        return true;
    }
    return false;
}
void IrohTransport::Broadcast(const std::shared_ptr<Data>& message) {
    for (const auto& session : Sessions()) static_cast<void>(SendToSession(session, message));
}
bool IrohTransport::SendToStream(const std::string& stream_id, const std::shared_ptr<Data>& message) {
    if (!message) return false;
    for (const auto& session : Sessions()) {
        if (session->StreamId() == stream_id) {
            return SendToSession(session, message);
        }
    }
    return false;
}
FileTransferSendResult IrohTransport::SendFile(const std::string& stream_id, const std::shared_ptr<Data>& message, const std::string& binding_id) {
    for (const auto& session : Sessions()) {
        if (session->StreamId() == stream_id && (binding_id.empty() || session->BindingId() == binding_id)) return session->SendFile(message);
    }
    return FileTransferSendResult::Disconnected("iroh file route is unavailable");
}
bool IrohTransport::SubmitVideo(const std::string& monitor, const EncodedVideoFrameEvent& encoded) {
    const auto sessions = Sessions();
    if (sessions.empty() || !encoded.data_ || encoded.frame_width_ == 0 || encoded.frame_width_ > 65535 || encoded.frame_height_ == 0 ||
        encoded.frame_height_ > 65535 || (encoded.type_ != EncodedVideoType::kH264 && encoded.type_ != EncodedVideoType::kH265))
        return false;
    media::VideoFrame frame{};
    {
        std::lock_guard lock(mutex_);
        if (!monitor_slots_.contains(monitor)) {
            if (monitor_slots_.size() >= 256) return false;
            monitor_slots_.emplace(monitor, static_cast<std::uint8_t>(monitor_slots_.size()));
        }
        frame.stream = monitor_slots_.at(monitor);
    }
    frame.codec = encoded.type_ == EncodedVideoType::kH265 ? media::VideoCodec::kH265 : media::VideoCodec::kH264;
    frame.kind = encoded.key_frame_
                     ? media::VideoFrameKind::kIdr
                     : (encoded.reference_state_ == EncodedReferenceState::kRecoveryConfirmed ? media::VideoFrameKind::kReferenceRecovery
                                                                                              : media::VideoFrameKind::kPredicted);
    frame.monitor = monitor;
    frame.width = static_cast<std::uint16_t>(encoded.frame_width_);
    frame.height = static_cast<std::uint16_t>(encoded.frame_height_);
    frame.frame_index = encoded.frame_index_;
    frame.encoded.assign(encoded.data_->Bytes().begin(), encoded.data_->Bytes().end());
    for (const auto& session : sessions) static_cast<void>(session->SendVideo(frame));
    // The frame reached the sequenced transport boundary, including per-peer busy
    // drops. Each connection exposes that gap and owns asynchronous recovery.
    // Reporting it as a pre-packetization failure makes VideoBacklog request an
    // unthrottled IDR on every busy frame, defeating the transport recovery limit.
    return true;
}
void IrohTransport::UpdatePermissions(const std::string& stream_id, const std::vector<std::string>& permissions) {
    for (const auto& session : Sessions())
        if (session->StreamId() == stream_id) session->UpdatePermissions(permissions);
}
}  // namespace px
