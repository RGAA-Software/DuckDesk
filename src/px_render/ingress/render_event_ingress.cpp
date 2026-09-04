//
// Created by RGAA on 15/11/2024.
//

#include "render_event_ingress.h"
#include <fstream>
#include <unordered_set>
#include "rd_app.h"
#include "rd_context.h"
#include "px_message.pb.h"
#include "rd_statistics.h"
#include "px_render/modules/render_module_registry.h"
#include "px_common_new/log.h"
#include "px_common_new/privacy_log.h"
#include "px_common_new/data.h"
#include "px_common_new/image.h"
#include "px_render/ingress/network_event_ingress.h"
#include <nlohmann/json.hpp>
#include "px_render_panel_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_message_new/rp_proto_converter.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "architecture/encoders/video_encoder_module.h"
#include "architecture/sources/monitor_capture_source.h"
#include "px_render/modules/module_ids.h"
#include "px_capture_new/capture_message.h"
#include "architecture/services/file_transfer_types.h"
#include "architecture/services/file_transfer_service.h"
#include "architecture/services/voice_call_service.h"

using namespace nlohmann;

namespace px
{

    RenderEventIngress::RenderEventIngress(const std::shared_ptr<RdApplication>& app) {
        app_ = app;
        context_ = app->GetContext();
        module_registry_ = context_->GetRenderModuleRegistry();
        network_ingress_ = NetworkEventIngress::Make(app);
        msg_notifier_ = app_->GetContext()->GetMessageNotifier();
        stat_ = RdStatistics::Instance();
    }

    void RenderEventIngress::ProcessCompatibilityEvent(const std::shared_ptr<PxPluginBaseEvent>& event) {
        if (event->event_type_ == PxPluginEventType::kPluginNetClientEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginNetClientEvent>(event);
            network_ingress_->ProcessNetEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginClientConnectedEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginClientConnectedEvent>(event);
            network_ingress_->ProcessClientConnectedEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginClientDisConnectedEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginClientDisConnectedEvent>(event);
            network_ingress_->ProcessClientDisConnectedEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginFileTransferDisconnectedEvent) {
            const auto target_event =
                std::dynamic_pointer_cast<PxPluginFileTransferDisconnectedEvent>(event);
            if (!target_event) {
                return;
            }
            std::string binding_id;
            if (target_event->plugin_name_ == kNetWebRtcRemoteLibraryId) {
                binding_id = std::string("rtc:") + target_event->stream_id_;
            }
            else if (target_event->plugin_name_ == kNetWebRtcLocalLibraryId) {
                binding_id = std::string("rtc-local:") + target_event->stream_id_;
            }
            const auto registry = app_->GetLogicalSessionRegistry();
            const auto logical_session_id = registry && !binding_id.empty()
                ? registry->FindLogicalSessionIdByBinding(
                      binding_id, static_cast<int64_t>(TimeUtil::GetCurrentTimestamp()))
                : std::nullopt;
            if (!logical_session_id) {
                LOGI("Ignore stale FT-only disconnect without a logical binding: stream {}, plugin {}",
                     target_event->stream_id_, target_event->plugin_name_);
                return;
            }
            const FileTransferRouteDisconnected disconnected{
                .logical_session_id = *logical_session_id,
                .stream_id = target_event->stream_id_,
                .transport_id = target_event->plugin_name_,
                .connection_id = target_event->connection_instance_id_,
            };
            if (const auto service = context_->GetFileTransferService()) {
                service->HandleRouteDisconnected(disconnected);
            }
        }
        else if (event->event_type_ == PxPluginEventType::kPluginWebRtcVoicePcmEvent) {
            const auto pcm =
                std::dynamic_pointer_cast<PxPluginWebRtcVoicePcmEvent>(event);
            const auto service = context_->GetVoiceCallService();
            if (pcm && service && !pcm->pcm_.empty()) {
                service->HandleWebRtcPcm(
                    pcm->stream_id_, pcm->call_id_, pcm->pcm_,
                    pcm->sample_rate_, pcm->channels_);
            }
        }
        else if (event->event_type_ == PxPluginEventType::kPluginCapturingMonitorInfoEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginCapturingMonitorInfoEvent>(event);
            network_ingress_->ProcessCapturingMonitorInfoEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginInsertIdrEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginInsertIdrEvent>(event);
            // mon_name_ 为空 = 广播所有屏(旧行为);非空 = 只给目标屏补 IDR
            const auto mon_name = target_event ? target_event->mon_name_ : "";
            module_registry_->InsertIdr(mon_name);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginInvalidateRefFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginInvalidateRefFrameEvent>(event);
            const auto mon_name = target_event ? target_event->mon_name_ : "";
            const auto invalid_index = target_event ? target_event->invalid_frame_index_ : 0;
            const bool accepted = module_registry_->InvalidateReferenceFrame(
                mon_name, invalid_index);
            if (!accepted) {
                // 与 Sunshine 一致:编码器不支持 RFI(例如 FFmpeg 软编)时立即补 IDR,
                // 不要等客户端 2s 无帧超时再回退。
                LOGW("RFI not accepted by any encoder, fallback to IDR immediately.");
                module_registry_->InsertIdr(mon_name);
            }
        }
        else if (event->event_type_ == PxPluginEventType::kPluginRelayPausedEvent) {

        }
        else if (event->event_type_ == PxPluginEventType::kPluginRelayResumeEvent) {

        }
        else if (event->event_type_ == PxPluginEventType::kPluginRtcAnswerSdpEvent) {
            this->SendAnswerSdpToRemote(event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginRtcIceEvent) {
            this->SendIceToRemote(event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginRtcReportEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginRtcReportEvent>(event);
            network_ingress_->ProcessRtcReportEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginDataSent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginDataSent>(event);
            stat_->AppendMediaBytes(target_event->size_);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginRemoteClipboardResp) {
            auto target_event = std::dynamic_pointer_cast<PxPluginRemoteClipboardResp>(event);
            ReportRemoteClipboardResp(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginPanelStreamMessage) {
            const auto weak_self = weak_from_this();
            app_->PostGlobalTask([weak_self, event]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                auto target_event = std::dynamic_pointer_cast<PxPluginPanelStreamMessage>(event);
                self->ProcessPanelStreamMessage(target_event);
            });
        }
        else if (event->event_type_ == PxPluginEventType::kPluginConfigEncoder) {
            const auto weak_self = weak_from_this();
            app_->PostGlobalTask([weak_self, event]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                auto encoders = self->app_->GetWorkingVideoEncoders();
                auto target_event = std::dynamic_pointer_cast<PxPluginConfigEncoder>(event);
                // GetWorkingVideoEncoders 按屏索引,多屏会指向同一 plugin 实例;
                // 去重后再 Config,避免对同一 NVENC 插件连打多次。
                std::unordered_set<std::shared_ptr<VideoEncoderModule>>
                    unique_encoders;
                for (const auto& [mon_name, plugin] : encoders) {
                    if (plugin) {
                        unique_encoders.insert(plugin);
                    }
                }
                for (const auto& plugin : unique_encoders) {
                    plugin->Reconfigure(target_event->mon_name_, target_event->bps_, target_event->fps_);
                }
            });
        }
        else if (event->event_type_ == PxPluginEventType::kPluginRelayAlive) {
            auto target_event = std::dynamic_pointer_cast<PxPluginRelayAlive>(event);
            ReportRelayAlive(target_event->device_id_, (int64_t)target_event->created_timestamp_);
            //LOGI("Plugin update relay alive: {} -> {}", target_event->device_id_, target_event->created_timestamp_);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginReqParamsBeginStreaming) {
            auto target_event = std::dynamic_pointer_cast<PxPluginReqParamsBeginStreaming>(event);
            //LOGI("ReqParamsBeginStreaming, stream id: {}, force gdi: {}", target_event->stream_id_, target_event->force_gdi_);
            app_->HandleForceGdiEvent(target_event->force_gdi_);
        }
        else if (event->event_type_ ==
                 PxPluginEventType::kPluginSelectCaptureMonitorEvent) {
            const auto target_event = std::dynamic_pointer_cast<
                PxPluginSelectCaptureMonitorEvent>(event);
            const auto capture = app_->GetWorkingMonitorCaptureSource();
            if (target_event && capture &&
                !target_event->monitor_name_.empty()) {
                capture->SelectMonitor(target_event->monitor_name_);
            }
        }
        else if (event->event_type_ == PxPluginEventType::kPluginRedeemConnectionTicket) {
            auto target_event = std::dynamic_pointer_cast<PxPluginRedeemConnectionTicketEvent>(event);
            if (target_event) {
                app_->RedeemConnectionTicket(
                    target_event->ticket_,
                    target_event->client_nonce_,
                    target_event->instance_id_,
                    std::move(target_event->callback_));
            }
        }
        else if (event->event_type_ == PxPluginEventType::kPluginAdmitLogicalSession) {
            const auto target_event =
                std::dynamic_pointer_cast<PxPluginAdmitLogicalSessionEvent>(event);
            if (!target_event || !target_event->callback_) {
                return;
            }
            const auto registry = app_->GetLogicalSessionRegistry();
            if (!registry) {
                target_event->callback_(LogicalSessionAdmission{});
                return;
            }
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const auto admission = registry->Bind(
                target_event->grant_, target_event->transport_, target_event->binding_id_,
                target_event->takeover_, now_ms);
            if (admission.release_previous_controller_input && network_ingress_) {
                network_ingress_->ReleaseControllerInput(LogicalSessionInputLease{
                    .logical_session_id = admission.previous_controller_session_id,
                    .generation = admission.previous_controller_lease_generation,
                });
                const auto previous_stream = registry->FindStreamId(
                    admission.previous_controller_session_id);
                if (previous_stream.has_value()) {
                    const auto capability_update = PxLogicalSessionCapabilityUpdate{
                        .stream_id_ = *previous_stream,
                        .permissions_ = {"view", "audio"},
                    };
                    module_registry_->ApplyLogicalSessionCapabilities(
                        capability_update);
                }
            }
            target_event->callback_(admission);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginCloseLogicalSessionBinding) {
            const auto target_event =
                std::dynamic_pointer_cast<PxPluginCloseLogicalSessionBindingEvent>(event);
            const auto registry = app_->GetLogicalSessionRegistry();
            if (!target_event || !registry || target_event->logical_session_id_.empty()
                || target_event->binding_id_.empty()) {
                return;
            }
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const auto closed = registry->CloseBinding(
                target_event->logical_session_id_, target_event->binding_id_, now_ms);
            if (closed.release_controller_input && network_ingress_) {
                network_ingress_->ReleaseControllerInput(LogicalSessionInputLease{
                    .logical_session_id = closed.logical_session_id,
                    .binding_id = target_event->binding_id_,
                    .generation = closed.lease_generation,
                });
            }
        }
    }

    void RenderEventIngress::SendAnswerSdpToRemote(const std::shared_ptr<PxPluginBaseEvent>& event) {
        auto target_event = std::dynamic_pointer_cast<PxPluginRtcAnswerSdpEvent>(event);
        auto stream_id = target_event->stream_id_;

        px::Message pt_msg;
        pt_msg.set_type(MessageType::kSigAnswerSdpMessage);
        auto sub = pt_msg.mutable_sig_answer_sdp();
        sub->set_sdp(target_event->sdp_);
        auto msg = ProtoAsData(&pt_msg);

        module_registry_->SendRelaySignalingMessage(stream_id, msg);
        LOGI("Send SDP by relay: {}", stream_id);
    }

    void RenderEventIngress::SendIceToRemote(const std::shared_ptr<PxPluginBaseEvent>& event) {
        auto target_event = std::dynamic_pointer_cast<PxPluginRtcIceEvent>(event);
        auto stream_id = target_event->stream_id_;

        px::Message pt_msg;
        pt_msg.set_type(MessageType::kSigIceMessage);
        auto sub = pt_msg.mutable_sig_ice();
        sub->set_ice(target_event->ice_);
        sub->set_mid(target_event->mid_);
        sub->set_sdp_mline_index(target_event->sdp_mline_index_);
        auto msg = ProtoAsData(&pt_msg);//.SerializeAsString();

        const auto ice = target_event->ice_;
        module_registry_->SendRelaySignalingMessage(stream_id, msg);
        LOGI("Send ICE by relay: {}", ice);
    }

    void RenderEventIngress::ReportRemoteClipboardResp(const std::shared_ptr<PxPluginRemoteClipboardResp>& event) {
        // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
        // Panel echo path replaced by UserProxy local echo when applying remote clipboard.
        (void)event;
    }

    void RenderEventIngress::ProcessPanelStreamMessage(const std::shared_ptr<PxPluginPanelStreamMessage>& event) {
        if (!event->body_) {
            return;
        }

        try {
            LOGI("ProcessPanelStreamMessage: {}", event->body_->AsString());
            json obj = json::parse(event->body_->AsString());
            auto event = obj["event"].get<std::string>();
            auto from_device = obj["from_device"].get<std::string>();
            if (event == "restart_render") {
                app_->SendAppMessage(MsgPanelStreamRestartRender {
                    .from_device_ = from_device,
                });
            }
            else if (event == "lock_screen") {
                app_->SendAppMessage(MsgPanelStreamLockScreen {
                    .from_device_ = from_device,
                });
            }
            else if (event == "restart_device") {
                app_->SendAppMessage(MsgPanelStreamRestartDevice {
                    .from_device_ = from_device,
                });
            }
            else if (event == "shutdown_device") {
                app_->SendAppMessage(MsgPanelStreamShutdownDevice {
                    .from_device_ = from_device,
                });
            }
        }
        catch(const std::exception& e) {
            LOGE("ProcessPanelStreamMessage failed: {}, body: {}", e.what(), event->body_->AsString());
        }
    }

    void RenderEventIngress::ReportRelayAlive(const std::string& device_id, int64_t timestamp) {
        // 不走 PostGlobalTask:全局任务在 render 主线程消息循环上执行,
        // 会话建立/高负载时主线程繁忙会把 alive 上报卡住数秒,导致 panel 指示灯误红。
        // PostPanelMessage 直接投递到 ws 网络线程,任意线程调用都是安全的。
        pxrp::RpMessage msg;
        msg.set_type(pxrp::kRpRelayAlive);
        auto sub = msg.mutable_relay_alive();
        sub->set_device_id(device_id);
        sub->set_timestamp(timestamp);
        auto buffer = RpProtoAsData(&msg);
        app_->PostPanelMessage(buffer);
    }

}
