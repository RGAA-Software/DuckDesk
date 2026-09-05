//
// Created RGAA on 15/11/2024.
//

#include "ws_transport.h"
#include "app/app_messages.h"

#include <utility>

#include "ws_server.h"
#include "px_common/log.h"
#include "px_common/data.h"
#include "px_capture/capture_message.h"
#include "px_render/modules/module_ids.h"
#include "px_render/architecture/events/render_event.h"
#include "px_render/architecture/runtime/render_execution_context.h"

namespace px {

WsTransport::WsTransport(std::shared_ptr<PxAsyncRuntime> async_runtime) : async_runtime_(std::move(async_runtime)) {}

std::string WsTransport::Id() const {
    return kNetWsTransportId;
}

std::string WsTransport::Name() const {
    return "Net WebSocket";
}

std::string WsTransport::VersionName() const {
    return "1.1.0";
}

uint32_t WsTransport::VersionCode() const {
    return 110;
}

std::string WsTransport::Description() const {
    return "Network via WebSocket";
}

bool WsTransport::Start(const px::RenderModuleConfiguration& configuration) {
    if (!RenderModule::Start(configuration)) {
        return false;
    }
    game_hook_mode_ = configuration.app_mode == "game-hook";
    auto listen_port = configuration.ws_listen_port;
    auto config_listen_port = std::int64_t{0};
    if (config_listen_port > 0) {
        listen_port = config_listen_port;
    }
    const auto self = std::dynamic_pointer_cast<WsTransport>(shared_from_this());
    const std::weak_ptr<WsTransport> weak_self = self;
    if (weak_self.expired()) {
        LOGE("event=module.start component=net_ws code=MODULE_DEPENDENCY_UNAVAILABLE "
             "operation=create_server outcome=failed recoverable=false "
             "reason=ws_transport_requires_shared_ownership");
        return false;
    }
    ws_server_ = std::make_shared<WsServer>(weak_self, async_runtime_, static_cast<uint16_t>(listen_port));
    if (!ws_server_->Start()) {
        ws_server_.reset();
        RenderModule::Stop();
        return false;
    }
    return true;
}

bool WsTransport::Destroy() {
    RenderModule::Stop();
    if (ws_server_) {
        ws_server_->Exit();
        ws_server_.reset();
    }
    return RenderModule::Destroy();
}

PxAwaitable<PxResult<void>> WsTransport::StopAsync(std::shared_ptr<WsTransport> owner, const std::chrono::steady_clock::time_point deadline) {
    if (!owner) {
        co_return PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "net-ws.stop", "WS transport owner is missing"));
    }
    owner->RenderModule::Stop();
    const auto server = owner->ws_server_;
    if (server) {
        const auto stopped = co_await WsServer::StopAsync(server, deadline);
        if (!stopped) {
            co_return stopped;
        }
    }
    owner->ws_server_.reset();
    co_return PxResult<void>::Success();
}

void WsTransport::ApplyLogicalSessionCapabilities(const PxLogicalSessionCapabilityUpdate& update) {
    if (ws_server_) {
        ws_server_->UpdateLogicalSessionCapabilities(update);
    }
}

void WsTransport::Tick1Second() {
    // 兜底清扫 /ipc 允许集合里进程已死的 pid(断线未触发/异常退出场景)
    if (IsWorking() && ws_server_) {
        ws_server_->SweepDeadIpcPids();
        ws_server_->ReportPerformance();
    }
}

bool WsTransport::IsWorking() const {
    return ws_server_ && ws_server_->IsWorking();
}

void WsTransport::Broadcast(std::shared_ptr<Data> msg, bool run_through) {
    if (IsWorking() && HasConnectedClients() && msg) {
        const auto server = ws_server_;
        static_cast<void>(execution_context_->Post([server, msg = std::move(msg)]() {
            if (server && server->IsWorking()) {
                server->PostNetMessage(msg);
            }
        }));
    }
}

bool WsTransport::SendToStream(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
    if (IsWorking() && HasConnectedClients() && msg) {
        return ws_server_->PostTargetStreamMessage(stream_id, msg);
    }
    return false;
}

FileTransferSendResult WsTransport::SendFileTransfer(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through,
                                                     const std::string& connection_instance_id) {
    // A file-transfer-only client intentionally opens /file/transfer without
    // a companion /stream connection.  HasConnectedClients() reports media
    // stream routers, so using it here drops every response for a standalone
    // file manager even though the target FT router is alive.  Let the FT
    // router lookup below be the source of truth for this channel.
    if (!msg) {
        return FileTransferSendResult::TransportError("WebSocket file-transfer payload is empty");
    }
    if (IsWorking()) {
        return ws_server_->PostTargetFileTransferMessage(stream_id, msg, connection_instance_id);
    }
    return FileTransferSendResult::Disconnected("WebSocket file-transfer server is not working");
}

void WsTransport::SendUserProxy(std::shared_ptr<Data> msg) {
    if (IsWorking() && msg && ws_server_) {
        const auto server = ws_server_;
        static_cast<void>(execution_context_->Post([server, msg = std::move(msg)]() {
            if (server && server->IsWorking()) {
                server->PostUserProxyMessage(msg);
            }
        }));
    }
}

void WsTransport::SendIpc(std::shared_ptr<Data> msg) {
    if (!IsWorking() || !msg || !ws_server_) {
        return;
    }
    const auto server = ws_server_;
    static_cast<void>(execution_context_->Post([server, msg = std::move(msg)]() {
        if (server && server->IsWorking()) {
            server->PostIpcBinaryMessage(msg);
        }
    }));
}

void WsTransport::RegisterIpcPid(uint32_t pid) {
    if (!IsWorking() || !ws_server_) {
        return;
    }
    const auto server = ws_server_;
    static_cast<void>(execution_context_->Post([server, pid]() {
        if (server && server->IsWorking()) {
            server->RegisterIpcPid(pid);
        }
    }));
}

bool WsTransport::IsUserProxyConnected() {
    if (IsWorking() && ws_server_) {
        return ws_server_->IsUserProxyConnected();
    }
    return false;
}

bool WsTransport::HasOnlyAudioClients() {
    if (IsWorking()) {
        return ws_server_->IsOnlyAudioClients();
    } else {
        return false;
    }
}

int WsTransport::ConnectedClientCount() {
    if (IsWorking()) {
        return ws_server_->GetConnectedClientsCount();
    } else {
        return 0;
    }
}

int64_t WsTransport::QueuedMediaCount() {
    if (IsWorking()) {
        return ws_server_->GetQueuingMediaMsgCount();
    } else {
        return 0;
    }
}

int64_t WsTransport::QueuedFileTransferCount() {
    if (IsWorking()) {
        return ws_server_->GetQueuingFtMsgCount();
    } else {
        return 0;
    }
}

bool WsTransport::HasMediaCapacity() const noexcept {
    return true;
}

bool WsTransport::HasFileTransferCapacity() const noexcept {
    return true;
}

bool WsTransport::HasConnectedClients() {
    return ConnectedClientCount() > 0;
}

std::vector<std::shared_ptr<PxConnectedClientInfo>> WsTransport::ConnectedClients() {
    if (IsWorking()) {
        return ws_server_->GetConnectedClientInfo();
    }
    return {};
}

void WsTransport::HandleAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
    if (event->type_ == AppBaseEvent::EType::kClientHello) {
        auto target_event = std::dynamic_pointer_cast<MsgClientHello>(event);
        if (ws_server_) {
            ws_server_->OnClientHello(target_event);
        }
    }
}

void WsTransport::ConfigureNetworkServices(NetworkBroadcaster network_broadcaster, FileTransferBroadcaster file_transfer_broadcaster,
                                           LocalRtcAllocator local_rtc_allocator, UdpAssociationUpdater udp_association_updater) {
    std::scoped_lock lock(network_services_mutex_);
    network_broadcaster_ = std::move(network_broadcaster);
    file_transfer_broadcaster_ = std::move(file_transfer_broadcaster);
    local_rtc_allocator_ = std::move(local_rtc_allocator);
    udp_association_updater_ = std::move(udp_association_updater);
}

void WsTransport::BroadcastNetworkMessage(const std::shared_ptr<Data>& message, const bool run_through) const {
    NetworkBroadcaster broadcaster;
    {
        std::scoped_lock lock(network_services_mutex_);
        broadcaster = network_broadcaster_;
    }
    if (broadcaster && message) {
        broadcaster(message, run_through);
    }
}

void WsTransport::BroadcastFileTransferMessage(const std::string& stream_id, const std::shared_ptr<Data>& message, const bool run_through) const {
    FileTransferBroadcaster broadcaster;
    {
        std::scoped_lock lock(network_services_mutex_);
        broadcaster = file_transfer_broadcaster_;
    }
    if (broadcaster && message) {
        broadcaster(stream_id, message, run_through);
    }
}

PxLocalRtcAllocResult WsTransport::AllocateLocalRtcInstance(const std::shared_ptr<PxLocalRtcRequestInfo>& request,
                                                            LocalRtcCompletion completion) const {
    LocalRtcAllocator allocator;
    {
        std::scoped_lock lock(network_services_mutex_);
        allocator = local_rtc_allocator_;
    }
    return allocator ? allocator(request, std::move(completion)) : PxLocalRtcAllocResult::kFailed;
}

bool WsTransport::HasLocalRtcService() const {
    std::scoped_lock lock(network_services_mutex_);
    return static_cast<bool>(local_rtc_allocator_);
}

bool WsTransport::UpdateUdpAssociation(const UdpMediaAssociation& association) const {
    UdpAssociationUpdater updater;
    {
        std::scoped_lock lock(network_services_mutex_);
        updater = udp_association_updater_;
    }
    if (!updater) {
        return false;
    }
    return updater(association);
}

void WsTransport::ConfigureIpcMediaIngress(IpcVideoFrameSink video_sink, IpcAudioFrameSink audio_sink) {
    std::scoped_lock lock(ipc_media_ingress_mutex_);
    ipc_video_frame_sink_ = std::move(video_sink);
    ipc_audio_frame_sink_ = std::move(audio_sink);
}

void WsTransport::SubmitIpcVideoFrame(const CaptureVideoFrame& frame) const {
    IpcVideoFrameSink sink;
    {
        std::scoped_lock lock(ipc_media_ingress_mutex_);
        sink = ipc_video_frame_sink_;
    }
    if (sink) {
        sink(frame);
    }
}

void WsTransport::SubmitIpcAudioFrame(const CaptureAudioFrame& frame) const {
    IpcAudioFrameSink sink;
    {
        std::scoped_lock lock(ipc_media_ingress_mutex_);
        sink = ipc_audio_frame_sink_;
    }
    if (sink) {
        sink(frame);
    }
}

void WsTransport::ReceiveClientEvent(const bool is_proto, const std::int64_t socket_fd, const TransportKind transport_type,
                                     const TransportChannel channel_type, std::shared_ptr<Data> message, std::string connection_instance_id) {
    auto event = std::make_shared<NetworkClientEvent>();
    event->is_proto_ = is_proto;
    event->socket_fd_ = socket_fd;
    event->transport_type_ = transport_type;
    event->channel_type_ = channel_type;
    event->message_ = std::move(message);
    event->connection_instance_id_ = std::move(connection_instance_id);
    const auto weak_self = std::weak_ptr<WsTransport>(std::dynamic_pointer_cast<WsTransport>(shared_from_this()));
    event->ack_callback_ = [weak_self](const std::shared_ptr<NetMessageAck>& ack) {
        if (const auto self = weak_self.lock()) {
            self->HandleMessageAck(ack);
        }
    };
    EmitEvent(event);
}

void WsTransport::ReceiveClientEventImmediately(const bool is_proto, const std::int64_t socket_fd, const TransportKind transport_type,
                                                const TransportChannel channel_type, std::shared_ptr<Data> message,
                                                std::string connection_instance_id) {
    auto event = std::make_shared<NetworkClientEvent>();
    event->is_proto_ = is_proto;
    event->socket_fd_ = socket_fd;
    event->transport_type_ = transport_type;
    event->channel_type_ = channel_type;
    event->message_ = std::move(message);
    event->connection_instance_id_ = std::move(connection_instance_id);
    const auto weak_self = std::weak_ptr<WsTransport>(std::dynamic_pointer_cast<WsTransport>(shared_from_this()));
    event->ack_callback_ = [weak_self](const std::shared_ptr<NetMessageAck>& ack) {
        if (const auto self = weak_self.lock()) {
            self->HandleMessageAck(ack);
        }
    };
    EmitEventImmediately(event);
}

void WsTransport::HandleMessageAck(const std::shared_ptr<NetMessageAck>& ack) {
    // LOGI("OnMessage ack, type: {}, channel: {}, resp time: {}", ack->msg_type_, (int)ack->ch_type_, ack->resp_time_);
    if (ack->ch_type_ == TransportChannel::kFileTransfer) {
        if (last_ack_) {
            auto diff = ack->resp_time_ - last_ack_->resp_time_;
            LOGI("OnMessage ack: {}ms", (diff));
        }
        last_ack_ = ack;
    }
}

void WsTransport::SubmitEncodedVideo(const std::string& mon_name, const EncodedVideoType& video_type, const std::shared_ptr<Data>& data,
                                     uint64_t frame_index, int frame_width, int frame_height, bool key) {
    if (!mon_name.empty()) {
        std::lock_guard<std::mutex> lk(capturing_mon_mtx_);
        capturing_monitor_name_ = mon_name;
    }
}

std::string WsTransport::CapturingMonitorName() {
    std::lock_guard<std::mutex> lk(capturing_mon_mtx_);
    if (capturing_monitor_name_.empty() && game_hook_mode_) {
        // game hook 模式输入按游戏窗口 rect 换算,不需要显示器名;
        // hook 编码帧不带 mon_name,直接给占位名,
        // 否则 Web 端要轮询 /get/render/configuration 15s 才启用输入回传
        return "game_hook";
    }
    return capturing_monitor_name_;
}

} // namespace px
