//
// Created RGAA on 15/11/2024.
//

#include "ws_plugin.h"
#include "ws_server.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugin_interface/px_plugin_context.h"

PX_PLUGIN_EXPORT(px::WsPlugin)

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
        ws_server_ = std::make_shared<WsPluginServer>(this, (uint16_t)listen_port);
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

    void WsPlugin::OnMessageRaw(const std::any& msg) {
        if (HoldsType<PxLogicalSessionCapabilityUpdate>(msg) && ws_server_) {
            ws_server_->UpdateLogicalSessionCapabilities(
                std::any_cast<PxLogicalSessionCapabilityUpdate>(msg));
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
            return ws_server_->PostTargetFileTransferMessage(stream_id, msg);
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

    PxNetPlugin* WsPlugin::GetLocalRtcPlugin() {
        if (auto plugin = GetPluginById(kNetRtcLocalPluginId); plugin) {
            return (PxNetPlugin*)plugin;
        }
        return nullptr;
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
