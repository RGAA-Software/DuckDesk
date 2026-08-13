//
// Created by RGAA on 15/11/2024.
//

#include "plugin_event_router.h"
#include <fstream>
#include <unordered_set>
#include "rd_app.h"
#include "rd_context.h"
#include "tc_message.pb.h"
#include "rd_statistics.h"
#include "plugin_manager.h"
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "tc_common_new/image.h"
#include "plugin_net_event_router.h"
#include <nlohmann/json.hpp>
#include "tc_render_panel_message.pb.h"
#include "tc_message_new/proto_converter.h"
#include "tc_message_new/rp_proto_converter.h"
#include "gr_render/plugin_interface/gr_plugin_events.h"
#include "gr_render/plugin_interface/gr_stream_plugin.h"
#include "gr_render/plugin_interface/gr_video_encoder_plugin.h"
#include "plugin_stream_event_router.h"
#include "tc_capture_new/capture_message.h"

using namespace nlohmann;

namespace tc
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

    void PluginEventRouter::ProcessPluginEvent(const std::shared_ptr<GrPluginBaseEvent>& event) {
        // encoded video frame
        if (event->event_type_ == GrPluginEventType::kPluginEncodedVideoFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginEncodedVideoFrameEvent>(event);
            stream_event_router_->ProcessEncodedVideoFrameEvent(target_event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginNetClientEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginNetClientEvent>(event);
            net_event_router_->ProcessNetEvent(target_event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginClientConnectedEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginClientConnectedEvent>(event);
            net_event_router_->ProcessClientConnectedEvent(target_event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginClientDisConnectedEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginClientDisConnectedEvent>(event);
            net_event_router_->ProcessClientDisConnectedEvent(target_event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginCapturedVideoFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginCapturedVideoFrameEvent>(event);
            msg_notifier_->SendAppMessage(target_event->frame_);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginCapturingMonitorInfoEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginCapturingMonitorInfoEvent>(event);
            net_event_router_->ProcessCapturingMonitorInfoEvent(target_event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginCursorEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginCursorEvent>(event);
            msg_notifier_->SendAppMessage(target_event->cursor_info_);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginRawVideoFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginRawVideoFrameEvent>(event);
            auto msg = CaptureVideoFrame{};
            msg.frame_width_ = target_event->image_->width;
            msg.frame_height_ = target_event->image_->height;
            msg.frame_index_ = target_event->frame_index_;
            msg.raw_image_ = target_event->image_;
            msg.adapter_uid_ = -1;
            msg.frame_format_ = target_event->frame_format_;
            msg_notifier_->SendAppMessage(msg);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginRawAudioFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginRawAudioFrameEvent>(event);
            auto msg = CaptureAudioFrame{};
            msg.samples_ = target_event->sample_rate_;
            msg.channels_ = target_event->channels_;
            msg.bits_ = target_event->bits_;
            msg.full_data_ = target_event->full_data_;
            msg_notifier_->SendAppMessage(msg);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginSplitRawAudioFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginSplitRawAudioFrameEvent>(event);
            auto msg = CaptureAudioFrame{};
            msg.samples_ = target_event->sample_rate_;
            msg.channels_ = target_event->channels_;
            msg.bits_ = target_event->bits_;
            msg.left_ch_data_ = target_event->left_ch_data_;
            msg.right_ch_data_ = target_event->right_ch_data_;
            msg_notifier_->SendAppMessage(msg);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginEncodedAudioFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginEncodedAudioFrameEvent>(event);
            net_event_router_->ProcessEncodedAudioFrameEvent(target_event->data_,
                                                             target_event->sample_rate_,
                                                             target_event->channels_,
                                                             target_event->bits_,
                                                             target_event->frame_size_);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginInsertIdrEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginInsertIdrEvent>(event);
            // mon_name_ 为空 = 广播所有屏(旧行为);非空 = 只给目标屏补 IDR
            const auto mon_name = target_event ? target_event->mon_name_ : "";
            plugin_manager_->VisitEncoderPlugins([=, this](GrVideoEncoderPlugin* plugin) {
                // TODO:
                //LOGI("Insert IDR for plugin: {}", plugin->GetPluginName());
                plugin->InsertIdr(mon_name);
            });
        }
        else if (event->event_type_ == GrPluginEventType::kPluginInvalidateRefFrameEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginInvalidateRefFrameEvent>(event);
            const auto mon_name = target_event ? target_event->mon_name_ : "";
            const auto invalid_index = target_event ? target_event->invalid_frame_index_ : 0;
            bool accepted = false;
            plugin_manager_->VisitEncoderPlugins([&](GrVideoEncoderPlugin* plugin) {
                accepted = plugin->InvalidateRefFrame(mon_name, invalid_index) || accepted;
            });
            if (!accepted) {
                // 与 Sunshine 一致:编码器不支持 RFI(例如 FFmpeg 软编)时立即补 IDR,
                // 不要等客户端 2s 无帧超时再回退。
                LOGW("RFI not accepted by any encoder, fallback to IDR immediately.");
                plugin_manager_->VisitEncoderPlugins([&](GrVideoEncoderPlugin* plugin) {
                    plugin->InsertIdr(mon_name);
                });
            }
        }
        else if (event->event_type_ == GrPluginEventType::kPluginRelayPausedEvent) {

        }
        else if (event->event_type_ == GrPluginEventType::kPluginRelayResumeEvent) {

        }
        else if (event->event_type_ == GrPluginEventType::kPluginRtcAnswerSdpEvent) {
            this->SendAnswerSdpToRemote(event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginRtcIceEvent) {
            this->SendIceToRemote(event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginRtcReportEvent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginRtcReportEvent>(event);
            net_event_router_->ProcessRtcReportEvent(target_event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginFileTransferBegin) {
            auto target_event = std::dynamic_pointer_cast<GrPluginFileTransferBegin>(event);
            ReportFileTransferBegin(target_event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginFileTransferEnd) {
            auto target_event = std::dynamic_pointer_cast<GrPluginFileTransferEnd>(event);
            ReportFileTransferEnd(target_event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginDataSent) {
            auto target_event = std::dynamic_pointer_cast<GrPluginDataSent>(event);
            stat_->AppendMediaBytes(target_event->size_);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginRemoteClipboardResp) {
            auto target_event = std::dynamic_pointer_cast<GrPluginRemoteClipboardResp>(event);
            ReportRemoteClipboardResp(target_event);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginPanelStreamMessage) {
            app_->PostGlobalTask([=, this]() {
                auto target_event = std::dynamic_pointer_cast<GrPluginPanelStreamMessage>(event);
                ProcessPanelStreamMessage(target_event);
            });
        }
        else if (event->event_type_ == GrPluginEventType::kPluginConfigEncoder) {
            app_->PostGlobalTask([=, this]() {
                auto plugins = app_->GetWorkingVideoEncoderPlugins();
                auto target_event = std::dynamic_pointer_cast<GrPluginConfigEncoder>(event);
                // GetWorkingVideoEncoderPlugins 按屏索引,多屏会指向同一 plugin 实例;
                // 去重后再 Config,避免对同一 NVENC 插件连打多次。
                std::unordered_set<GrVideoEncoderPlugin*> unique_plugins;
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
        else if (event->event_type_ == GrPluginEventType::kPluginRelayAlive) {
            auto target_event = std::dynamic_pointer_cast<GrPluginRelayAlive>(event);
            ReportRelayAlive(target_event->device_id_, (int64_t)target_event->created_timestamp_);
            //LOGI("Plugin update relay alive: {} -> {}", target_event->device_id_, target_event->created_timestamp_);
        }
        else if (event->event_type_ == GrPluginEventType::kPluginReqParamsBeginStreaming) {
            auto target_event = std::dynamic_pointer_cast<GrPluginReqParamsBeginStreaming>(event);
            //LOGI("ReqParamsBeginStreaming, stream id: {}, force gdi: {}", target_event->stream_id_, target_event->force_gdi_);
            app_->HandleForceGdiEvent(target_event->force_gdi_);
        }
    }

    void PluginEventRouter::SendAnswerSdpToRemote(const std::shared_ptr<GrPluginBaseEvent>& event) {
        auto target_event = std::dynamic_pointer_cast<GrPluginRtcAnswerSdpEvent>(event);
        auto stream_id = target_event->stream_id_;

        tc::Message pt_msg;
        pt_msg.set_type(MessageType::kSigAnswerSdpMessage);
        auto sub = pt_msg.mutable_sig_answer_sdp();
        sub->set_sdp(target_event->sdp_);
        auto msg = ProtoAsData(&pt_msg);

        plugin_manager_->VisitNetPlugins([=, this](GrNetPlugin* plugin) {
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

    void PluginEventRouter::SendIceToRemote(const std::shared_ptr<GrPluginBaseEvent>& event) {
        auto target_event = std::dynamic_pointer_cast<GrPluginRtcIceEvent>(event);
        auto stream_id = target_event->stream_id_;

        tc::Message pt_msg;
        pt_msg.set_type(MessageType::kSigIceMessage);
        auto sub = pt_msg.mutable_sig_ice();
        sub->set_ice(target_event->ice_);
        sub->set_mid(target_event->mid_);
        sub->set_sdp_mline_index(target_event->sdp_mline_index_);
        auto msg = ProtoAsData(&pt_msg);//.SerializeAsString();

        plugin_manager_->VisitNetPlugins([=, this](GrNetPlugin* plugin) {
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

    void PluginEventRouter::ReportFileTransferBegin(const std::shared_ptr<GrPluginFileTransferBegin>& event) {
        app_->PostGlobalTask([=, this]() {
            tcrp::RpMessage msg;
            msg.set_type(tcrp::kRpFileTransferBegin);
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

    void PluginEventRouter::ReportFileTransferEnd(const std::shared_ptr<GrPluginFileTransferEnd>& event) {
        app_->PostGlobalTask([=, this]() {
            tcrp::RpMessage msg;
            msg.set_type(tcrp::kRpFileTransferEnd);
            auto sub = msg.mutable_ft_end();
            sub->set_the_file_id(event->the_file_id_);
            sub->set_end_timestamp(event->end_timestamp_);
            sub->set_success(event->success_);
            auto buffer = RpProtoAsData(&msg);
            app_->PostPanelMessage(buffer);
        });
    }

    void PluginEventRouter::ReportRemoteClipboardResp(const std::shared_ptr<GrPluginRemoteClipboardResp>& event) {
        // USER_PROXY_MIGRATION: clipboard path disabled, see gr_user_proxy
        // Panel echo path replaced by UserProxy local echo when applying remote clipboard.
        (void)event;
#if 0
        app_->PostGlobalTask([=, this]() {
            tcrp::RpMessage msg;
            msg.set_type(tcrp::kRpRemoteClipboardResp);
            auto sub = msg.mutable_remote_clipboard_resp();
            sub->set_content_type(event->content_type_);
            sub->set_msg(event->remote_info_);
            auto buffer = RpProtoAsData(&msg);
            app_->PostPanelMessage(buffer);
        });
#endif
    }

    void PluginEventRouter::ProcessPanelStreamMessage(const std::shared_ptr<GrPluginPanelStreamMessage>& event) {
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
        tcrp::RpMessage msg;
        msg.set_type(tcrp::kRpRelayAlive);
        auto sub = msg.mutable_relay_alive();
        sub->set_device_id(device_id);
        sub->set_timestamp(timestamp);
        auto buffer = RpProtoAsData(&msg);
        app_->PostPanelMessage(buffer);
    }

}
