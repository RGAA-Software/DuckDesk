//
// Created RGAA on 15/11/2024.
//

#include "ws_plugin.h"
#include "ws_server.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_capture_new/capture_message.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugin_interface/px_plugin_context.h"


namespace px
{

    WsPlugin::WsPlugin() : PxNetPlugin() {

    }

    std::string WsPlugin::GetPluginId() {
        return kNetWsPluginId;
    }

    std::string WsPlugin::GetPluginName() {
        return "Net WebSocket";
    }

    std::string WsPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t WsPlugin::GetVersionCode() {
        return 110;
    }

    std::string WsPlugin::GetPluginDescription() {
        return "Network via WebSocket";
    }

    bool WsPlugin::OnCreate(const px::PxPluginParam& param) {
        PxPluginInterface::OnCreate(param);
        game_hook_mode_ = GetConfigStringParam("app_mode") == "game-hook";
        auto listen_port = GetConfigIntParam("ws-listen-port");
        auto config_listen_port = GetConfigIntParam("listen-port");
        if (config_listen_port > 0) {
            listen_port = config_listen_port;
        }
        const auto weak_self = weak_from_this();
        if (weak_self.expired()) {
            LOGE("event=module.start component=net_ws code=MODULE_DEPENDENCY_UNAVAILABLE "
                 "operation=create_server outcome=failed recoverable=false "
                 "reason=ws_plugin_requires_shared_ownership");
            return false;
        }
        ws_server_ = std::make_shared<WsPluginServer>(
            weak_self, static_cast<uint16_t>(listen_port));
        ws_server_->Start();
        return true;
    }

    bool WsPlugin::OnDestroy() {
        PxNetPlugin::OnStop();
        if (ws_server_) {
            ws_server_->Exit();
            ws_server_.reset();
        }
        return PxNetPlugin::OnDestroy();
    }

    void WsPlugin::ApplyLogicalSessionCapabilities(
        const PxLogicalSessionCapabilityUpdate& update) {
        if (ws_server_) {
            ws_server_->UpdateLogicalSessionCapabilities(update);
        }
    }

    void WsPlugin::On1Second() {
        // 兜底清扫 /ipc 允许集合里进程已死的 pid(断线未触发/异常退出场景)
        if (IsWorking() && ws_server_) {
            ws_server_->SweepDeadIpcPids();
        }
    }

    bool WsPlugin::IsWorking() {
        return ws_server_ && ws_server_->IsWorking();
    }

    void WsPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
        if (IsWorking() && HasConnectedClients() && msg) {
            const auto server = ws_server_;
            plugin_context_->PostWorkTask([server, msg = std::move(msg)]() {
                if (server && server->IsWorking()) {
                    server->PostNetMessage(msg);
                }
            });
        }
    }

    bool WsPlugin::PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        if (IsWorking() && HasConnectedClients() && msg) {
            return ws_server_->PostTargetStreamMessage(stream_id, msg);
        }
        return false;
    }

    FileTransferSendResult WsPlugin::PostTargetFileTransferProtoMessage(
        const std::string& stream_id,
        std::shared_ptr<Data> msg,
        bool run_through,
        const std::string& connection_instance_id) {
        // A file-transfer-only client intentionally opens /file/transfer without
        // a companion /stream connection.  HasConnectedClients() reports media
        // stream routers, so using it here drops every response for a standalone
        // file manager even though the target FT router is alive.  Let the FT
        // router lookup below be the source of truth for this channel.
        if (!msg) {
            return FileTransferSendResult::TransportError(
                "WebSocket file-transfer payload is empty");
        }
        if (IsWorking()) {
            return ws_server_->PostTargetFileTransferMessage(
                stream_id, msg, connection_instance_id);
        }
        return FileTransferSendResult::Disconnected(
            "WebSocket file-transfer server is not working");
    }

    void WsPlugin::PostUserProxyMessage(std::shared_ptr<Data> msg) {
        if (IsWorking() && msg && ws_server_) {
            const auto server = ws_server_;
            plugin_context_->PostWorkTask([server, msg = std::move(msg)]() {
                if (server && server->IsWorking()) {
                    server->PostUserProxyMessage(msg);
                }
            });
        }
    }

    void WsPlugin::PostIpcBinaryMessage(std::shared_ptr<Data> msg) {
        if (!IsWorking() || !msg || !ws_server_) {
            return;
        }
        const auto server = ws_server_;
        plugin_context_->PostWorkTask([server, msg = std::move(msg)]() {
            if (server && server->IsWorking()) {
                server->PostIpcBinaryMessage(msg);
            }
        });
    }

    void WsPlugin::RegisterIpcPid(uint32_t pid) {
        if (!IsWorking() || !ws_server_) {
            return;
        }
        const auto server = ws_server_;
        plugin_context_->PostWorkTask([server, pid]() {
            if (server && server->IsWorking()) {
                server->RegisterIpcPid(pid);
            }
        });
    }

    bool WsPlugin::IsUserProxyConnected() {
        if (IsWorking() && ws_server_) {
            return ws_server_->IsUserProxyConnected();
        }
        return false;
    }

    bool WsPlugin::IsOnlyAudioClients() {
        if (IsWorking()) {
            return ws_server_->IsOnlyAudioClients();
        } else {
            return false;
        }
    }

    int WsPlugin::GetConnectedClientsCount() {
        if (IsWorking()) {
            return ws_server_->GetConnectedClientsCount();
        } else {
            return 0;
        }
    }

    int64_t WsPlugin::GetQueuingMediaMsgCount() {
        if (IsWorking()) {
            return ws_server_->GetQueuingMediaMsgCount();
        } else {
            return 0;
        }
    }

    int64_t WsPlugin::GetQueuingFtMsgCount() {
        if (IsWorking()) {
            return ws_server_->GetQueuingFtMsgCount();
        } else {
            return 0;
        }
    }

    bool WsPlugin::HasEnoughBufferForQueuingMediaMessages() {
        return true;
    }

    bool WsPlugin::HasEnoughBufferForQueuingFtMessages() {
        return true;
    }

    bool WsPlugin::HasConnectedClients() {
        return GetConnectedClientsCount() > 0;
    }

    std::vector<std::shared_ptr<PxConnectedClientInfo>> WsPlugin::GetConnectedClientInfo() {
        if (IsWorking()) {
            return ws_server_->GetConnectedClientInfo();
        }
        return {};
    }

    void WsPlugin::DispatchAppEvent(const std::shared_ptr<AppBaseEvent> &event) {
        if (event->type_ == AppBaseEvent::EType::kClientHello) {
            auto target_event = std::dynamic_pointer_cast<MsgClientHello>(event);
            if (ws_server_) {
                ws_server_->OnClientHello(target_event);
            }
        }
    }

    void WsPlugin::ConfigureNetworkServices(
        NetworkBroadcaster network_broadcaster,
        FileTransferBroadcaster file_transfer_broadcaster,
        LocalRtcAllocator local_rtc_allocator,
        UdpAssociationUpdater udp_association_updater) {
        std::scoped_lock lock(network_services_mutex_);
        network_broadcaster_ = std::move(network_broadcaster);
        file_transfer_broadcaster_ = std::move(file_transfer_broadcaster);
        local_rtc_allocator_ = std::move(local_rtc_allocator);
        udp_association_updater_ = std::move(udp_association_updater);
    }

    void WsPlugin::BroadcastNetworkMessage(
        const std::shared_ptr<Data>& message,
        const bool run_through) const {
        NetworkBroadcaster broadcaster;
        {
            std::scoped_lock lock(network_services_mutex_);
            broadcaster = network_broadcaster_;
        }
        if (broadcaster && message) {
            broadcaster(message, run_through);
        }
    }

    void WsPlugin::BroadcastFileTransferMessage(
        const std::string& stream_id,
        const std::shared_ptr<Data>& message,
        const bool run_through) const {
        FileTransferBroadcaster broadcaster;
        {
            std::scoped_lock lock(network_services_mutex_);
            broadcaster = file_transfer_broadcaster_;
        }
        if (broadcaster && message) {
            broadcaster(stream_id, message, run_through);
        }
    }

    PxLocalRtcAllocResult WsPlugin::AllocateLocalRtcInstance(
        const std::shared_ptr<PxLocalRtcRequestInfo>& request,
        LocalRtcCompletion completion) const {
        LocalRtcAllocator allocator;
        {
            std::scoped_lock lock(network_services_mutex_);
            allocator = local_rtc_allocator_;
        }
        return allocator
            ? allocator(request, std::move(completion))
            : PxLocalRtcAllocResult::kFailed;
    }

    bool WsPlugin::HasLocalRtcService() const {
        std::scoped_lock lock(network_services_mutex_);
        return static_cast<bool>(local_rtc_allocator_);
    }

    bool WsPlugin::UpdateUdpAssociation(
        const UdpMediaAssociation& association) const {
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

    void WsPlugin::ConfigureIpcMediaIngress(
        IpcVideoFrameSink video_sink,
        IpcAudioFrameSink audio_sink) {
        std::scoped_lock lock(ipc_media_ingress_mutex_);
        ipc_video_frame_sink_ = std::move(video_sink);
        ipc_audio_frame_sink_ = std::move(audio_sink);
    }

    void WsPlugin::SubmitIpcVideoFrame(
        const CaptureVideoFrame& frame) const {
        IpcVideoFrameSink sink;
        {
            std::scoped_lock lock(ipc_media_ingress_mutex_);
            sink = ipc_video_frame_sink_;
        }
        if (sink) {
            sink(frame);
        }
    }

    void WsPlugin::SubmitIpcAudioFrame(
        const CaptureAudioFrame& frame) const {
        IpcAudioFrameSink sink;
        {
            std::scoped_lock lock(ipc_media_ingress_mutex_);
            sink = ipc_audio_frame_sink_;
        }
        if (sink) {
            sink(frame);
        }
    }

    void WsPlugin::OnMessageAck(const std::shared_ptr<NetMessageAck> &ack) {
        //LOGI("OnMessage ack, type: {}, channel: {}, resp time: {}", ack->msg_type_, (int)ack->ch_type_, ack->resp_time_);
        if (ack->ch_type_ == NetChannelType::kFileTransfer) {
            if (last_ack_) {
                auto diff = ack->resp_time_ - last_ack_->resp_time_;
                LOGI("OnMessage ack: {}ms", (diff));
            }
            last_ack_ = ack;
        }
    }

    void WsPlugin::OnEncodedVideoFrame(const std::string& mon_name,
                                       const PxPluginEncodedVideoType& video_type,
                                       const std::shared_ptr<Data>& data,
                                       uint64_t frame_index,
                                       int frame_width,
                                       int frame_height,
                                       bool key) {
        if (!mon_name.empty()) {
            std::lock_guard<std::mutex> lk(capturing_mon_mtx_);
            capturing_mon_name_ = mon_name;
        }
    }

    std::string WsPlugin::GetCapturingMonitorName() {
        std::lock_guard<std::mutex> lk(capturing_mon_mtx_);
        if (capturing_mon_name_.empty() && game_hook_mode_) {
            // game hook 模式输入按游戏窗口 rect 换算,不需要显示器名;
            // hook 编码帧不带 mon_name,直接给占位名,
            // 否则 Web 端要轮询 /get/render/configuration 15s 才启用输入回传
            return "game_hook";
        }
        return capturing_mon_name_;
    }

}
