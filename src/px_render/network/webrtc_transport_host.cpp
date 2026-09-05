#include "network/webrtc_transport_host.h"

#include <atomic>
#include <utility>
#include <variant>

#include "px_common_new/callback_quiescence.h"
#include "px_common_new/log.h"
#include "px_render/network/webrtc/local/webrtc_local_transport.h"
#include "px_render/network/webrtc/remote/webrtc_remote_transport.h"

namespace px {

class WebRtcTransportHandle::State final {
  public:
    using Transport = std::variant<std::shared_ptr<WebRtcRemoteTransport>, std::shared_ptr<WebRtcLocalTransport>>;

    State(std::string base_name, const WebRtcTransportKind kind, Transport transport)
        : base_name_(std::move(base_name)), kind_(kind), transport_(std::move(transport)), callback_quiescence_(PxCallbackQuiescence::Create()) {}

    template <typename Operation> decltype(auto) Visit(Operation&& operation) const {
        return std::visit([&operation](const auto& transport) -> decltype(auto) { return operation(transport); }, transport_);
    }

    [[nodiscard]] std::shared_ptr<WebRtcLocalTransport> Local() const {
        if (std::holds_alternative<std::shared_ptr<WebRtcLocalTransport>>(transport_)) {
            return std::get<std::shared_ptr<WebRtcLocalTransport>>(transport_);
        }
        return {};
    }

    void BeginStop() {
        callback_quiescence_->BeginStop();
        Visit([](const auto& transport) { transport->SetEventCallback({}); });
        if (!stop_requested_.exchange(true, std::memory_order_acq_rel)) {
            Visit([](const auto& transport) { transport->Stop(); });
        }
    }

    void DestroyTransport() {
        BeginStop();
        if (!destroyed_.exchange(true, std::memory_order_acq_rel)) {
            Visit([](const auto& transport) { transport->Destroy(); });
        }
    }

    [[nodiscard]] bool IsAccepting() const {
        return callback_quiescence_->IsAccepting();
    }

    const std::string base_name_;
    const WebRtcTransportKind kind_;
    const Transport transport_;
    const std::shared_ptr<PxCallbackQuiescence> callback_quiescence_;
    std::atomic_bool started_{false};
    std::atomic_bool stop_requested_{false};
    std::atomic_bool destroyed_{false};
};

WebRtcTransportHandle::WebRtcTransportHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}

WebRtcTransportHandle::~WebRtcTransportHandle() {
    state_->DestroyTransport();
}

WebRtcTransportKind WebRtcTransportHandle::Kind() const {
    return state_->kind_;
}

std::string WebRtcTransportHandle::BaseName() const {
    return state_->base_name_;
}

WebRtcTransportInfo WebRtcTransportHandle::Info() const {
    return state_->Visit([](const auto& transport) { return transport->Info(); });
}

bool WebRtcTransportHandle::Start(const WebRtcTransportConfiguration& configuration) {
    const auto started = state_->Visit([&configuration](const auto& transport) { return transport->Start(configuration); });
    state_->started_.store(started, std::memory_order_release);
    return started;
}

void WebRtcTransportHandle::Stop() {
    state_->BeginStop();
}

void WebRtcTransportHandle::Destroy() {
    state_->DestroyTransport();
}

PxAwaitable<PxResult<void>> WebRtcTransportHandle::StopAsync(std::shared_ptr<WebRtcTransportHandle> owner,
                                                             const std::chrono::steady_clock::time_point deadline) {
    if (!owner) {
        co_return PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "webrtc.stop", "WebRTC library owner is missing"));
    }
    const auto started = std::chrono::steady_clock::now();
    owner->state_->DestroyTransport();
    const auto quiescent =
        co_await PxCallbackQuiescence::WaitUntilQuiescent(owner->state_->callback_quiescence_, deadline, "webrtc.callback_quiescence");
    if (!quiescent) {
        LOGE("event=webrtc.callback_quiescence component={} code=WEBRTC_CALLBACK_QUIESCENCE_TIMEOUT operation=stop outcome=timeout "
             "recoverable=false outstanding={} reason={}",
             owner->BaseName(), owner->OutstandingCallbacks(), quiescent.Error().message);
        co_return PxResult<void>::Failure(quiescent.Error());
    }
    LOGI("event=webrtc.callback_quiescence component={} operation=stop outcome=success outstanding=0 duration_ms={}", owner->BaseName(),
         std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    co_return PxResult<void>::Success();
}

std::uint64_t WebRtcTransportHandle::OutstandingCallbacks() const {
    return state_->callback_quiescence_->Outstanding();
}

void WebRtcTransportHandle::SetEventCallback(WebRtcEventCallback callback) {
    if (!callback || !state_->IsAccepting()) {
        state_->Visit([](const auto& transport) { transport->SetEventCallback({}); });
        return;
    }
    const auto weak_gate = std::weak_ptr<PxCallbackQuiescence>(state_->callback_quiescence_);
    state_->Visit([weak_gate, callback = std::move(callback)](const auto& transport) mutable {
        transport->SetEventCallback([weak_gate, callback = std::move(callback)](const WebRtcEvent& event) {
            const auto gate = weak_gate.lock();
            if (!gate) {
                return;
            }
            auto lease = gate->TryEnter();
            if (lease) {
                callback(event);
            }
        });
    });
}

void WebRtcTransportHandle::SetEnabled(const bool enabled) {
    if (state_->IsAccepting()) {
        state_->Visit([enabled](const auto& transport) { transport->SetEnabled(enabled); });
    }
}

bool WebRtcTransportHandle::IsWorking() const {
    return state_->Visit([](const auto& transport) { return transport->IsWorking(); });
}

void WebRtcTransportHandle::UpdateSettings(const WebRtcTransportSettings& settings) {
    if (state_->IsAccepting()) {
        state_->Visit([&settings](const auto& transport) { transport->UpdateSettings(settings); });
    }
}

void WebRtcTransportHandle::Send(const std::shared_ptr<Data>& message, const bool run_through) {
    if (state_->IsAccepting()) {
        state_->Visit([&](const auto& transport) { transport->PostProtoMessage(message, run_through); });
    }
}

bool WebRtcTransportHandle::SendToStream(const std::string& stream_id, const std::shared_ptr<Data>& message, const bool run_through) {
    return state_->IsAccepting() &&
           state_->Visit([&](const auto& transport) { return transport->PostTargetStreamProtoMessage(stream_id, message, run_through); });
}

FileTransferSendResult WebRtcTransportHandle::SendFileTransfer(const std::string& stream_id, const std::shared_ptr<Data>& message,
                                                               const bool run_through, const std::string& connection_instance_id) {
    if (!state_->IsAccepting()) {
        return FileTransferSendResult::Disconnected("WebRTC library is stopping");
    }
    return state_->Visit([&](const auto& transport) {
        return transport->PostTargetFileTransferProtoMessage(stream_id, message, run_through, connection_instance_id);
    });
}

void WebRtcTransportHandle::SubmitRawAudio(const std::shared_ptr<Data>& data, const int samples, const int channels, const int bits) {
    if (const auto local = state_->Local(); local && state_->IsAccepting()) {
        local->OnRawAudioData(data, samples, channels, bits);
    }
}

void WebRtcTransportHandle::SubmitEncodedVideo(const std::string& monitor_name, const WebRtcEncodedVideoType video_type,
                                               const std::shared_ptr<Data>& data, const std::uint64_t frame_index, const int frame_width,
                                               const int frame_height, const bool key_frame) {
    if (const auto local = state_->Local(); local && state_->IsAccepting()) {
        local->OnEncodedVideoFrame(monitor_name, video_type, data, frame_index, frame_width, frame_height, key_frame);
    }
}

void WebRtcTransportHandle::ApplyLogicalSessionCapabilities(const PxLogicalSessionCapabilityUpdate& update) {
    if (state_->IsAccepting()) {
        state_->Visit([&update](const auto& transport) { transport->ApplyLogicalSessionCapabilities(update); });
    }
}

int WebRtcTransportHandle::ConnectedClientCount() const {
    return state_->Visit([](const auto& transport) { return transport->GetConnectedClientsCount(); });
}

int WebRtcTransportHandle::MediaConsumerCount() const {
    if (const auto local = state_->Local()) {
        return local->GetMediaConsumersCount();
    }
    return ConnectedClientCount();
}

bool WebRtcTransportHandle::HasVideoClient() const {
    return IsWorking() && ConnectedClientCount() > 0;
}

std::int64_t WebRtcTransportHandle::QueuedMediaMessageCount() const {
    return state_->Visit([](const auto& transport) { return transport->GetQueuingMediaMsgCount(); });
}

std::int64_t WebRtcTransportHandle::QueuedFileTransferMessageCount() const {
    return state_->Visit([](const auto& transport) { return transport->GetQueuingFtMsgCount(); });
}

std::vector<std::shared_ptr<PxConnectedClientInfo>> WebRtcTransportHandle::ConnectedClients() const {
    return {};
}

void WebRtcTransportHandle::SubmitLocalSharedTexture(const std::string& monitor_name, const std::uint64_t frame_index, const int frame_width,
                                                     const int frame_height, const std::uint64_t shared_handle, const std::int64_t adapter_id,
                                                     const std::uint64_t frame_format) {
    if (const auto local = state_->Local(); local && state_->IsAccepting()) {
        local->OnRawVideoFrameSharedTexture(monitor_name, frame_index, frame_width, frame_height, shared_handle, adapter_id, frame_format);
    }
}

void WebRtcTransportHandle::SubmitLocalYuv(const std::string& monitor_name, const std::uint64_t frame_index, const int frame_width,
                                           const int frame_height, const std::shared_ptr<Image>& image) {
    if (const auto local = state_->Local(); local && state_->IsAccepting()) {
        local->OnRawVideoFrameYuv(monitor_name, frame_index, frame_width, frame_height, image);
    }
}

void WebRtcTransportHandle::UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& message) {
    if (const auto local = state_->Local(); local && state_->IsAccepting()) {
        local->UpdateCaptureMonitorInfo(message);
    }
}

void WebRtcTransportHandle::ApplyRemoteSdp(const MsgRtcRemoteSdp& message) {
    if (state_->IsAccepting()) {
        state_->Visit([&message](const auto& transport) { transport->ApplyRtcRemoteSdp(message); });
    }
}

void WebRtcTransportHandle::ApplyRemoteIce(const MsgRtcRemoteIce& message) {
    if (state_->IsAccepting()) {
        state_->Visit([&message](const auto& transport) { transport->ApplyRtcRemoteIce(message); });
    }
}

PxLocalRtcAllocResult WebRtcTransportHandle::AllocateLocalInstance(const std::shared_ptr<PxLocalRtcRequestInfo>& request,
                                                                   std::function<void(const std::shared_ptr<PxLocalRtcReplyInfo>&)> completion) {
    const auto local = state_->Local();
    if (!local || !state_->IsAccepting() || !completion) {
        return PxLocalRtcAllocResult::kFailed;
    }
    const auto weak_gate = std::weak_ptr<PxCallbackQuiescence>(state_->callback_quiescence_);
    return local->AllocNewLocalRtcInstance(request,
                                           [weak_gate, completion = std::move(completion)](const std::shared_ptr<PxLocalRtcReplyInfo>& reply) {
                                               const auto gate = weak_gate.lock();
                                               if (!gate) {
                                                   return;
                                               }
                                               auto lease = gate->TryEnter();
                                               if (lease) {
                                                   completion(reply);
                                               }
                                           });
}

bool WebRtcTransportHandle::SetVoiceAuthorization(const std::string& stream_id, const std::string& call_id, const bool authorized) {
    const auto local = state_->Local();
    return local && state_->IsAccepting() && local->SetVoiceCallAuthorization(stream_id, call_id, authorized);
}

bool WebRtcTransportHandle::SubmitVoicePcm(const std::string& stream_id, const std::string& call_id,
                                           const std::shared_ptr<const std::vector<std::int16_t>>& samples, const int sample_rate,
                                           const int channels) {
    const auto local = state_->Local();
    if (!local || !state_->IsAccepting() || !samples || samples->empty()) {
        return false;
    }
    local->OnVoiceCallPcm(stream_id, call_id, *samples, sample_rate, channels);
    return true;
}

std::shared_ptr<WebRtcTransportHost> WebRtcTransportHost::Create() {
    return std::make_shared<WebRtcTransportHost>();
}

WebRtcTransportHost::~WebRtcTransportHost() {
    Reset();
}

std::vector<std::shared_ptr<WebRtcTransportHandle>> WebRtcTransportHost::CreateTransports() {
    if (!transports_.empty()) {
        return transports_;
    }
    auto remote_state = std::make_shared<WebRtcTransportHandle::State>("px_render_rtc_remote", WebRtcTransportKind::kRemote,
                                                                       WebRtcTransportHandle::State::Transport{CreateWebRtcRemoteTransport()});
    transports_.push_back(std::make_shared<WebRtcTransportHandle>(std::move(remote_state)));
    auto local_state = std::make_shared<WebRtcTransportHandle::State>("px_render_rtc", WebRtcTransportKind::kLocal,
                                                                      WebRtcTransportHandle::State::Transport{CreateWebRtcLocalTransport()});
    transports_.push_back(std::make_shared<WebRtcTransportHandle>(std::move(local_state)));
    LOGI("event=webrtc.library.link component=webrtc_transport_host libraries=2 outcome=success");
    return transports_;
}

void WebRtcTransportHost::Reset() {
    for (const auto& transport : transports_) {
        if (transport) {
            transport->Destroy();
        }
    }
    transports_.clear();
}

} // namespace px
