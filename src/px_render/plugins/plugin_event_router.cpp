//
// Created by RGAA on 15/11/2024.
//

#include "plugin_event_router.h"
#include <fstream>
#include <unordered_set>
#include "rd_app.h"
#include "rd_context.h"
#include "px_message.pb.h"
#include "rd_statistics.h"
#include "plugin_manager.h"
#include "px_common_new/log.h"
#include "px_common_new/privacy_log.h"
#include "px_common_new/data.h"
#include "px_common_new/image.h"
#include "plugin_net_event_router.h"
#include <nlohmann/json.hpp>
#include "px_render_panel_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_message_new/rp_proto_converter.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugin_interface/px_stream_plugin.h"
#include "px_render/plugin_interface/px_video_encoder_plugin.h"
#include "plugin_stream_event_router.h"
#include "px_capture_new/capture_message.h"

using namespace nlohmann;

namespace px
{

    PluginEventRouter::PluginEventRouter(const std::shared_ptr<RdApplication>& app) {
        app_ = app;
        context_ = app->GetContext();
        plugin_manager_ = context_->GetPluginManager();
        stream_event_router_ = std::make_shared<PluginStreamEventRouter>(app);
        net_event_router_ = std::make_shared<PluginNetEventRouter>(app);
        msg_notifier_ = app_->GetContext()->GetMessageNotifier();
        stat_ = RdStatistics::Instance();
    }

    void PluginEventRouter::ProcessPluginEvent(const std::shared_ptr<PxPluginBaseEvent>& event) {
        // encoded video frame
        if (event->event_type_ == PxPluginEventType::kPluginEncodedVideoFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginEncodedVideoFrameEvent>(event);
            stream_event_router_->ProcessEncodedVideoFrameEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginNetClientEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginNetClientEvent>(event);
            net_event_router_->ProcessNetEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginClientConnectedEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginClientConnectedEvent>(event);
            net_event_router_->ProcessClientConnectedEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginClientDisConnectedEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginClientDisConnectedEvent>(event);
            net_event_router_->ProcessClientDisConnectedEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginCapturedVideoFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginCapturedVideoFrameEvent>(event);
            msg_notifier_->SendAppMessage(target_event->frame_);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginCapturingMonitorInfoEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginCapturingMonitorInfoEvent>(event);
            net_event_router_->ProcessCapturingMonitorInfoEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginCursorEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginCursorEvent>(event);
            msg_notifier_->SendAppMessage(target_event->cursor_info_);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginRawVideoFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginRawVideoFrameEvent>(event);
            auto msg = CaptureVideoFrame{};
            msg.frame_width_ = target_event->image_->width;
            msg.frame_height_ = target_event->image_->height;
            msg.frame_index_ = target_event->frame_index_;
            msg.raw_image_ = target_event->image_;
            msg.adapter_uid_ = -1;
            msg.frame_format_ = target_event->frame_format_;
            msg_notifier_->SendAppMessage(msg);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginRawAudioFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginRawAudioFrameEvent>(event);
            auto msg = CaptureAudioFrame{};
            msg.samples_ = target_event->sample_rate_;
            msg.channels_ = target_event->channels_;
            msg.bits_ = target_event->bits_;
            msg.full_data_ = target_event->full_data_;
            msg_notifier_->SendAppMessage(msg);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginSplitRawAudioFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginSplitRawAudioFrameEvent>(event);
            auto msg = CaptureAudioFrame{};
            msg.samples_ = target_event->sample_rate_;
            msg.channels_ = target_event->channels_;
            msg.bits_ = target_event->bits_;
            msg.left_ch_data_ = target_event->left_ch_data_;
            msg.right_ch_data_ = target_event->right_ch_data_;
            msg_notifier_->SendAppMessage(msg);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginEncodedAudioFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginEncodedAudioFrameEvent>(event);
            net_event_router_->ProcessEncodedAudioFrameEvent(target_event->data_,
                                                             target_event->sample_rate_,
                                                             target_event->channels_,
                                                             target_event->bits_,
                                                             target_event->frame_size_);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginInsertIdrEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginInsertIdrEvent>(event);
            // mon_name_ 为空 = 广播所有屏(旧行为);非空 = 只给目标屏补 IDR
            const auto mon_name = target_event ? target_event->mon_name_ : "";
            plugin_manager_->VisitEncoderPlugins([=, this](PxVideoEncoderPlugin* plugin) {
                // TODO:
                //LOGI("Insert IDR for plugin: {}", plugin->GetPluginName());
                plugin->InsertIdr(mon_name);
            });
        }
        else if (event->event_type_ == PxPluginEventType::kPluginInvalidateRefFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginInvalidateRefFrameEvent>(event);
            const auto mon_name = target_event ? target_event->mon_name_ : "";
            const auto invalid_index = target_event ? target_event->invalid_frame_index_ : 0;
            bool accepted = false;
            plugin_manager_->VisitEncoderPlugins([&](PxVideoEncoderPlugin* plugin) {
                accepted = plugin->InvalidateRefFrame(mon_name, invalid_index) || accepted;
            });
            if (!accepted) {
                // 与 Sunshine 一致:编码器不支持 RFI(例如 FFmpeg 软编)时立即补 IDR,
                // 不要等客户端 2s 无帧超时再回退。
                LOGW("RFI not accepted by any encoder, fallback to IDR immediately.");
                plugin_manager_->VisitEncoderPlugins([&](PxVideoEncoderPlugin* plugin) {
                    plugin->InsertIdr(mon_name);
                });
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
            net_event_router_->ProcessRtcReportEvent(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginFileTransferBegin) {
            auto target_event = std::dynamic_pointer_cast<PxPluginFileTransferBegin>(event);
            ReportFileTransferBegin(target_event);
        }
        else if (event->event_type_ == PxPluginEventType::kPluginFileTransferEnd) {
            auto target_event = std::dynamic_pointer_cast<PxPluginFileTransferEnd>(event);
            ReportFileTransferEnd(target_event);
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
            app_->PostGlobalTask([=, this]() {
                auto target_event = std::dynamic_pointer_cast<PxPluginPanelStreamMessage>(event);
                ProcessPanelStreamMessage(target_event);
            });
        }
        else if (event->event_type_ == PxPluginEventType::kPluginConfigEncoder) {
            app_->PostGlobalTask([=, this]() {
                auto plugins = app_->GetWorkingVideoEncoderPlugins();
                auto target_event = std::dynamic_pointer_cast<PxPluginConfigEncoder>(event);
                // GetWorkingVideoEncoderPlugins 按屏索引,多屏会指向同一 plugin 实例;
                // 去重后再 Config,避免对同一 NVENC 插件连打多次。
                std::unordered_set<PxVideoEncoderPlugin*> unique_plugins;
                for (const auto& [mon_name, plugin] : plugins) {
                    if (plugin) {
                        unique_plugins.insert(plugin);
                    }
                }
                for (auto* plugin : unique_plugins) {
                    plugin->ConfigEncoder(target_event->mon_name_, target_event->bps_, target_event->fps_);
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
        else if (event->event_type_ == PxPluginEventType::kPluginVoiceCallConsent) {
            auto target_event = std::dynamic_pointer_cast<PxPluginVoiceCallConsentEvent>(event);
            if (!target_event) {
                LOGE("[VoiceCall] consent event RTTI conversion failed");
                return;
            }
            LOGI("[VoiceCall] routing consent {} to px_panel, call={}, stream={}, request={}",
                 target_event->show_ ? "request" : "cancel",
                 PrivacyLogId(target_event->call_id_), target_event->stream_id_, target_event->request_id_);
            pxrp::RpMessage message;
            if (target_event->show_) {
                message.set_type(pxrp::kRpVoiceCallConsentRequest);
                auto* request = message.mutable_voice_call_consent_request();
                request->set_visitor_device_id(target_event->visitor_device_id_);
                request->set_stream_id(target_event->stream_id_);
                request->set_call_id(target_event->call_id_);
                request->set_request_id(target_event->request_id_);
                request->set_expires_at_unix_ms(target_event->expires_at_unix_ms_);
                request->set_protocol_version(1);
            }
            else {
                message.set_type(pxrp::kRpVoiceCallConsentCancel);
                auto* cancel = message.mutable_voice_call_consent_cancel();
                cancel->set_stream_id(target_event->stream_id_);
                cancel->set_call_id(target_event->call_id_);
                cancel->set_request_id(target_event->request_id_);
                cancel->set_reason(target_event->reason_);
            }
            const bool delivered = app_->PostPanelMessage(RpProtoAsData(&message));
            LOGI("[VoiceCall] consent {} delivery to px_panel: {}",
                 target_event->show_ ? "request" : "cancel",
                 delivered ? "queued" : "unavailable");
            if (!delivered && target_event->show_) {
                LOGW("[VoiceCall] px_panel unavailable; rejecting consent request");
                auto decision = std::make_shared<MsgVoiceCallConsentDecision>();
                decision->stream_id_ = target_event->stream_id_;
                decision->call_id_ = target_event->call_id_;
                decision->request_id_ = target_event->request_id_;
                decision->accepted_ = false;
                decision->reason_ = "panel_unavailable";
                context_->DispatchAppEvent2Plugins(decision);
            }
        }
    }

    void PluginEventRouter::SendAnswerSdpToRemote(const std::shared_ptr<PxPluginBaseEvent>& event) {
        auto target_event = std::dynamic_pointer_cast<PxPluginRtcAnswerSdpEvent>(event);
        auto stream_id = target_event->stream_id_;

        px::Message pt_msg;
        pt_msg.set_type(MessageType::kSigAnswerSdpMessage);
        auto sub = pt_msg.mutable_sig_answer_sdp();
        sub->set_sdp(target_event->sdp_);
        auto msg = ProtoAsData(&pt_msg);

        plugin_manager_->VisitNetPlugins([=, this](PxNetPlugin* plugin) {
            if (plugin->GetPluginId() == kRelayPluginId) {
                if (stream_id.empty()) {
                    plugin->PostProtoMessage(msg, true);
                }
                else {
                    plugin->PostTargetStreamProtoMessage(stream_id, msg, true);
                }
                LOGI("Send SDP by relay: {}", stream_id);
            }
        });
    }

    void PluginEventRouter::SendIceToRemote(const std::shared_ptr<PxPluginBaseEvent>& event) {
        auto target_event = std::dynamic_pointer_cast<PxPluginRtcIceEvent>(event);
        auto stream_id = target_event->stream_id_;

        px::Message pt_msg;
        pt_msg.set_type(MessageType::kSigIceMessage);
        auto sub = pt_msg.mutable_sig_ice();
        sub->set_ice(target_event->ice_);
        sub->set_mid(target_event->mid_);
        sub->set_sdp_mline_index(target_event->sdp_mline_index_);
        auto msg = ProtoAsData(&pt_msg);//.SerializeAsString();

        plugin_manager_->VisitNetPlugins([=, this](PxNetPlugin* plugin) {
            if (plugin->GetPluginId() == kRelayPluginId) {
                if (stream_id.empty()) {
                    plugin->PostProtoMessage(msg, true);
                }
                else {
                    plugin->PostTargetStreamProtoMessage(stream_id, msg, true);
                }
                LOGI("Send ICE by relay: {}", target_event->ice_);
            }
        });
    }

    void PluginEventRouter::ReportFileTransferBegin(const std::shared_ptr<PxPluginFileTransferBegin>& event) {
        app_->PostGlobalTask([=, this]() {
            pxrp::RpMessage msg;
            msg.set_type(pxrp::kRpFileTransferBegin);
            auto sub = msg.mutable_ft_begin();
            sub->set_the_file_id(event->the_file_id_);
            sub->set_begin_timestamp(event->begin_timestamp_);
            sub->set_direction(event->direction_);
            sub->set_file_detail(event->file_detail_);
            sub->set_visitor_device_id(event->visitor_device_id_);
            auto buffer = RpProtoAsData(&msg);
            app_->PostPanelMessage(buffer);
        });
    }

    void PluginEventRouter::ReportFileTransferEnd(const std::shared_ptr<PxPluginFileTransferEnd>& event) {
        app_->PostGlobalTask([=, this]() {
            pxrp::RpMessage msg;
            msg.set_type(pxrp::kRpFileTransferEnd);
            auto sub = msg.mutable_ft_end();
            sub->set_the_file_id(event->the_file_id_);
            sub->set_end_timestamp(event->end_timestamp_);
            sub->set_success(event->success_);
            auto buffer = RpProtoAsData(&msg);
            app_->PostPanelMessage(buffer);
        });
    }

    void PluginEventRouter::ReportRemoteClipboardResp(const std::shared_ptr<PxPluginRemoteClipboardResp>& event) {
        // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
        // Panel echo path replaced by UserProxy local echo when applying remote clipboard.
        (void)event;
#if 0
        app_->PostGlobalTask([=, this]() {
            pxrp::RpMessage msg;
            msg.set_type(pxrp::kRpRemoteClipboardResp);
            auto sub = msg.mutable_remote_clipboard_resp();
            sub->set_content_type(event->content_type_);
            sub->set_msg(event->remote_info_);
            auto buffer = RpProtoAsData(&msg);
            app_->PostPanelMessage(buffer);
        });
#endif
    }

    void PluginEventRouter::ProcessPanelStreamMessage(const std::shared_ptr<PxPluginPanelStreamMessage>& event) {
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

    void PluginEventRouter::ReportRelayAlive(const std::string& device_id, int64_t timestamp) {
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
