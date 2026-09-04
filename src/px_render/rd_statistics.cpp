//
// Created by RGAA on 2024/3/6.
//

#include "rd_statistics.h"
#include "session/logical_session_registry.h"
#include "rd_context.h"
#include "rd_app.h"
#include "app/app_messages.h"
#include "px_render/modules/module_ids.h"
#include "settings/rd_settings.h"
#include "px_render/modules/render_module_registry.h"
#include "px_common_new/log.h"
#include "px_common_new/fps_stat.h"
#include "px_common_new/time_util.h"
#include "px_common_new/process_util.h"
#include "px_render_panel_message.pb.h"
#include "px_common_new/message_notifier.h"
#include "architecture/sources/monitor_capture_source.h"
#include "architecture/encoders/video_encoder_module.h"
#include "px_message_new/rp_proto_converter.h"
#include "architecture/processors/frame_resizer_processor.h"

namespace px
{
    /// ---
    constexpr auto kMaxDurationCount = 180;

    void MsgWorkingCaptureInfo::AppendCopyTextureDuration(int32_t duration) {
        copy_texture_durations_.push_back(duration);
        if (copy_texture_durations_.size() > kMaxDurationCount) {
            copy_texture_durations_.pop_front();
        }
    }

    std::vector<int32_t> MsgWorkingCaptureInfo::GetCopyTextureDurations() {
        std::vector<int32_t> result;
        for (const auto& v : copy_texture_durations_) {
            result.push_back(v);
        }
        return result;
    }

    void MsgWorkingCaptureInfo::AppendMapCvtTextureDuration(int32_t duration) {
        map_cvt_texture_durations_.push_back(duration);
        if (map_cvt_texture_durations_.size() > kMaxDurationCount) {
            map_cvt_texture_durations_.pop_front();
        }
    }

    std::vector<int32_t> MsgWorkingCaptureInfo::GetMapCvtTextureDurations() {
        std::vector<int32_t> result;
        for (const auto& v : map_cvt_texture_durations_) {
            result.push_back(v);
        }
        return result;
    }

    /// ----
    RdStatistics::RdStatistics() {
        settings_ = RdSettings::Instance();
    }

    void RdStatistics::SetApplication(const std::shared_ptr<RdApplication>& app) {
        app_ = app;
        context_ = app->GetContext();
        module_registry_ = context_->GetRenderModuleRegistry();
    }

    void RdStatistics::StartMonitor() {
        msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kState);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgTimer5000>([weak_self](const MsgTimer5000&) {
            if (const auto self = weak_self.lock()) {
                self->OnChecking();
            }
        });
    }

    void RdStatistics::Exit() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        module_registry_.reset();
        context_.reset();
        app_.reset();
    }

    void RdStatistics::IncreaseRunningTime() {
        running_time_++;
    }

    void RdStatistics::AppendMediaBytes(int bytes) {
        send_media_bytes_ += bytes;
    }

    void RdStatistics::AppendAudioFrameGap(uint32_t time) {
        if (audio_frame_gaps_.Size() >= kMaxStatCounts) {
            audio_frame_gaps_.RemoveFirst();
        }
        audio_frame_gaps_.PushBack(time);
    }

    void RdStatistics::CopyLeftSpectrum(const std::vector<double>& sp, int cpy_size) {
        if (left_spectrum_.Size() < cpy_size) {
            left_spectrum_.Resize(cpy_size);
        }
        left_spectrum_.CopyMemPartialFrom(sp, cpy_size);
    }

    std::vector<double> RdStatistics::GetLeftSpectrum() {
        std::vector<double> out;
        left_spectrum_.CopyMemTo(out);
        return out;
    }

    void RdStatistics::CopyRightSpectrum(const std::vector<double>& sp, int cpy_size) {
        if (right_spectrum_.Size() < cpy_size) {
            right_spectrum_.Resize(cpy_size);
        }
        right_spectrum_.CopyMemPartialFrom(sp, cpy_size);
    }

    std::vector<double> RdStatistics::GetRightSpectrum() {
        std::vector<double> out;
        right_spectrum_.CopyMemTo(out);
        return out;
    }

    std::shared_ptr<Data> RdStatistics::AsProtoMessage() {
        pxrp::RpMessage msg;
        msg.set_type(pxrp::RpMessageType::kRpCaptureStatistics);

        auto cst = msg.mutable_capture_statistics();
        audio_frame_gaps_.Visit([&](auto& v) {
            cst->mutable_audio_frame_gaps()->Add(v);
        });

        // from inner server
        cst->set_app_running_time(running_time_);
        // from inner server
        cst->set_server_send_media_data(send_media_bytes_);
        //
        const auto app = app_.lock();
        if (!app) {
            return RpProtoAsData(&msg);
        }
        auto video_capture_source = app->GetWorkingMonitorCaptureSource();
        auto video_encoders = app->GetWorkingVideoEncoders();
        const auto frame_resizer = context_->GetFrameResizerProcessor();
        if (video_capture_source && !video_encoders.empty()) {
            // encoder info

            auto captures_info = video_capture_source->WorkingCaptures();
            for (const auto& [name, info] : captures_info) {
                auto cp_info = cst->mutable_working_captures_info();
                auto item = cp_info->Add();
                item->set_target_name(info->target_name_);
                item->set_capturing_fps(info->fps_);
                item->set_capture_type(info->capture_type_);
                auto video_capture_gaps = item->mutable_video_capture_gaps();
                for (const auto& v : info->capture_gaps_) {
                    video_capture_gaps->Add(v);
                }

                // encoder
                if (video_encoders.contains(info->target_name_)) {
                    auto video_encoder = video_encoders[info->target_name_];
                    auto video_encoders_info = video_encoder->WorkingCaptures();
                    if (video_encoders_info.contains(info->target_name_)) {
                        auto encoder_info = video_encoders_info[info->target_name_];
                        item->set_encoder_name(encoder_info->encoder_name_);
                        item->set_encoding_fps(encoder_info->fps_);

                        auto encode_durations = item->mutable_encode_durations();
                        for (const auto& v : encoder_info->encode_durations_) {
                            encode_durations->Add(v);
                        }
                    }
                }
                item->set_capture_frame_width(info->capture_frame_width_);
                item->set_capture_frame_height(info->capture_frame_height_);

                // capture info
                //LOGI("Target name: {}", info->target_name_);
                if (auto app_cp_info = app_captures_info_.TryGet(info->target_name_); app_cp_info.has_value() && app_cp_info.value()) {
                    //LOGI("copy texture durations: {}", app_cp_info->copy_texture_durations_.size());
                    //LOGI("map&cvt texture durations: {}", app_cp_info->map_cvt_texture_durations_.size());
                    {
                        auto durations = item->mutable_copy_texture_durations();
                        for (const auto &v: app_cp_info.value()->copy_texture_durations_) {
                            durations->Add(v);
                        }
                    }
                    {
                        auto durations = item->mutable_map_cvt_texture_durations();
                        for (const auto& v : app_cp_info.value()->map_cvt_texture_durations_) {
                            durations->Add(v);
                        }
                    }
                }

                // resize info
                bool is_gdi_capture = module_registry_->IsGdiCapture(app->GetWorkingMonitorCaptureSource());
                if (settings_->encoder_.encode_res_type_ == Encoder::EncodeResolutionType::kOrigin || is_gdi_capture) {
                    item->set_resize_frame_width(0);
                    item->set_resize_frame_height(0);
                }
                else {
                    if (frame_resizer) {
                        const auto resize_info =
                            frame_resizer->Snapshot(info->target_name_);
                        if (resize_info) {
                            item->set_resize_frame_width(
                                resize_info->target_width);
                            item->set_resize_frame_height(
                                resize_info->target_height);
                        }
                    }
                }
            }
        }

        // A person can own a WS control binding, a UDP media endpoint and an
        // RTC/FT binding simultaneously. Report each logical session once;
        // physical plug-in counts remain transport diagnostics only.
        const auto registry = app->GetLogicalSessionRegistry();
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto sessions = registry ? registry->SnapshotActive(now_ms)
                                       : std::vector<LogicalSessionSnapshot>{};
        cst->set_connected_clients_count(static_cast<int32_t>(sessions.size()));
        for (const auto& session : sessions) {
            auto cc = cst->mutable_connected_clients()->Add();
            cc->set_device_id(session.subject_id);
            cc->set_stream_id(session.stream_id);
            cc->set_room_id(session.logical_session_id);
            cc->set_device_name(session.role == LogicalSessionRole::kController
                ? "Controller" : "Observer");
        }

        cst->set_relay_connected(module_registry_->IsRelayConnected());

        // audio capture
        cst->set_audio_capture_type("WASAPI");

        //
        cst->set_video_encode_type(video_encoder_format_ == Encoder::EncoderFormat::kHEVC
            ? pxrp::VideoType::kNetHevc
            : pxrp::VideoType::kNetH264);

        cst->set_audio_encode_type(pxrp::AudioEncodeType::kNetOpus);

        auto buffer = RpProtoAsData(&msg);
        return buffer;
    }

    void RdStatistics::IncreaseDDAFailedCount() {
        dda_failed_count_++;
    }

    void RdStatistics::OnChecking() {
        // Reserved for low-rate state sampling. Keep the timer callback cheap
        // until a concrete statistic is added.
    }

    std::shared_ptr<MsgWorkingCaptureInfo> RdStatistics::CaptureInfo(const std::string& name) {
        if (auto info = app_captures_info_.TryGet(name); info.has_value() && info.value()) {
            return info.value();
        }
        auto info = std::make_shared<MsgWorkingCaptureInfo>();
        app_captures_info_.Insert(name, info);
        return info;
    }

}
