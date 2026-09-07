//
// Created by RGAA on 2024-04-20.
//

#include "px_statistics.h"
#include "px_render_panel_message.pb.h"
#include "px_app_messages.h"
#include "px_context.h"
#include "px_common/log.h"
#include "px_common/client_id_extractor.h"

namespace px
{

    PxStatistics::~PxStatistics() {
        Exit();
    }

    void PxStatistics::RegisterEventListeners() {
        const auto context = context_.lock();
        if (!context) {
            return;
        }
        msg_listener_ = context->ObtainMessageListener(MessageExecutionLane::kState);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<MsgCaptureStatistics>([weak_self](const MsgCaptureStatistics& msg) {
            if (const auto self = weak_self.lock()) {
                self->ProcessCaptureStatistics(msg);
            }
        });

        msg_listener_->Listen<MsgServerAudioSpectrum>([weak_self](const MsgServerAudioSpectrum& msg) {
            if (const auto self = weak_self.lock()) {
                self->ProcessAudioSpectrum(msg);
            }
        });

        msg_listener_->Listen<MsgGrTimer1S>([weak_self](const MsgGrTimer1S&) {
            if (const auto self = weak_self.lock()) {
                self->Process1SCalculation();
            }
        });
    }

    void PxStatistics::Exit() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        context_.reset();
    }

    void PxStatistics::ProcessCaptureStatistics(const MsgCaptureStatistics& msg) {
//        this->audio_frame_gaps_.Clear();
//        for (auto& v : msg.statistics_->audio_frame_gaps()) {
//            this->audio_frame_gaps_.PushBack(v);
//        }
        this->audio_frame_gaps_.CopyFrom<>(msg.statistics_->audio_frame_gaps());

        // from inner server
        this->app_running_time = msg.statistics_->app_running_time();
        // from inner server
        this->server_send_media_bytes = msg.statistics_->server_send_media_data();

        // captures information
        this->encode_durations_.Clear();
        this->video_capture_gaps_.Clear();
        this->copy_texture_durations_.Clear();
        this->map_cvt_texture_durations_.Clear();
        {
            captures_info_.Clear();
            int size = msg.statistics_->working_captures_info_size();
            for (int i = 0; i < size; i++) {
                auto info = msg.statistics_->working_captures_info(i);
                auto cpy_info = std::make_shared<pxrp::RpMsgWorkingCaptureInfo>();
                cpy_info->CopyFrom(info);
                captures_info_.PushBack(cpy_info);

                if (video_capture_type_ != info.capture_type()) {
                    video_capture_type_ = info.capture_type();
                }

                // encode durations
                std::vector<int32_t> encode_durations;
                for (const auto& v : info.encode_durations()) {
                    encode_durations.push_back(v);
                }
                encode_durations_.Insert(info.target_name(), encode_durations);

                // video capture gaps
                std::vector<int32_t> video_capture_gaps;
                for (const auto& v : info.video_capture_gaps()) {
                    video_capture_gaps.push_back(v);
                }
                video_capture_gaps_.Insert(info.target_name(), video_capture_gaps);

                // copy texture durations
                std::vector<int32_t> copy_texture_durations;
                for (const auto& v : info.copy_texture_durations()) {
                    copy_texture_durations.push_back(v);
                }
                copy_texture_durations_.Insert(info.target_name(), copy_texture_durations);

                // map cvt texture durations
                std::vector<int32_t> map_cvt_texture_durations;
                for (const auto& v : info.map_cvt_texture_durations()) {
                    map_cvt_texture_durations.push_back(v);
                }
                map_cvt_texture_durations_.Insert(info.target_name(), map_cvt_texture_durations);
            }
        }

        connected_clients_ = msg.statistics_->connected_clients_count();
        connected_clients_info_.Clear();
        for (const auto& item : msg.statistics_->connected_clients()) {
            auto info = std::make_shared<pxrp::RpConnectedClientInfo>();
            info->CopyFrom(item);
            connected_clients_info_.PushBack(info);
        }
        const auto context = context_.lock();
        const auto weak_self = weak_from_this();
        if (context) {
            context->PostTask([weak_self]() {
                const auto self = weak_self.lock();
                const auto context = self ? self->context_.lock() : nullptr;
                if (!self || !context) {
                    return;
                }
                context->SendAppMessage(MsgUpdateConnectedClientsInfo {
                    .clients_info_ = self->GetConnectedClientsInfo(),
                });
            });
        }

        relay_connected_ = msg.statistics_->relay_connected();
        audio_capture_type_ = msg.statistics_->audio_capture_type();

        {
            auto type = msg.statistics_->video_encode_type();
            if (type == pxrp::VideoType::kNetH264) {
                video_encode_type_ = "H264";
            }
            else if (type == pxrp::VideoType::kNetHevc) {
                video_encode_type_ = "HEVC";
            }
            else if (type == pxrp::VideoType::kNetVp9) {
                video_encode_type_ = "VP9";
            }
            else {
                video_encode_type_ = "N/A";
            }
        }

    }

    void PxStatistics::ProcessAudioSpectrum(const MsgServerAudioSpectrum& msg) {
        this->audio_samples_ = msg.spectrum_->samples();
        this->audio_channels_ = msg.spectrum_->channels();
        this->audio_bits_ = msg.spectrum_->bits();
        if (this->left_spectrum_.Size() != msg.spectrum_->left_spectrum().size()) {
            this->left_spectrum_.Resize(msg.spectrum_->left_spectrum().size());
        }
        //memcpy(this->left_spectrum_.data(), msg.spectrum_->left_spectrum().data(), msg.spectrum_->left_spectrum().size() * sizeof(double));
        if (!this->left_spectrum_.CopyMemFrom<>(msg.spectrum_->left_spectrum())) {
            LOGE("copy left spectrum failed.");
        }

        if (this->right_spectrum_.Size() != msg.spectrum_->right_spectrum().size()) {
            this->right_spectrum_.Resize(msg.spectrum_->right_spectrum().size());
        }
        if (!this->right_spectrum_.CopyMemFrom<>(msg.spectrum_->right_spectrum())) {
            LOGE("copy left spectrum failed.");
        }
        //memcpy(this->right_spectrum_.data(), msg.spectrum_->right_spectrum().data(), msg.spectrum_->right_spectrum().size() * sizeof(double));
    }

    void PxStatistics::Process1SCalculation() {
        // speed
        send_speed_bytes = server_send_media_bytes - last_server_send_media_bytes;
        last_server_send_media_bytes = server_send_media_bytes.load();

        //
    }

    std::map<std::string, std::vector<int32_t>> PxStatistics::GetEncodeDurations() {
        std::map<std::string, std::vector<int32_t>> r;
        encode_durations_.VisitAll([&](auto k, auto& v) {
            r.insert({k, v});
        });
        return r;
    }

    std::map<std::string, std::vector<int32_t>> PxStatistics::GetVideoCaptureGaps() {
        std::map<std::string, std::vector<int32_t>> r;
        video_capture_gaps_.VisitAll([&](auto k, auto& v) {
            r.insert({k, v});
        });
        return r;
    }

    std::map<std::string, std::vector<int32_t>> PxStatistics::GetCopyTextureDurations() {
        std::map<std::string, std::vector<int32_t>> r;
        copy_texture_durations_.VisitAll([&](auto k, auto& v) {
            r.insert({k, v});
        });
        return r;
    }

    std::map<std::string, std::vector<int32_t>> PxStatistics::GetMapCvtTextureDurations() {
        std::map<std::string, std::vector<int32_t>> r;
        map_cvt_texture_durations_.ApplyAll([&](auto k, auto& v) {
            r.insert({k, v});
        });
        return r;
    }

    std::vector<int32_t> PxStatistics::GetAudioFrameGaps() {
        std::vector<int32_t> r;
        audio_frame_gaps_.Visit([&](auto& v) {
            r.push_back((int32_t)v);
        });
        return r;
    }

    std::vector<double> PxStatistics::GetLeftSpectrum() {
        std::vector<double> r;
        left_spectrum_.Visit([&](auto& v) {
            r.push_back(v);
        });
        return r;
    }

    std::vector<double> PxStatistics::GetRightSpectrum() {
        std::vector<double> r;
        right_spectrum_.Visit([&](auto& v) {
            r.push_back(v);
        });
        return r;
    }

    std::vector<std::shared_ptr<pxrp::RpMsgWorkingCaptureInfo>> PxStatistics::GetCapturesInfo() {
        std::vector<std::shared_ptr<pxrp::RpMsgWorkingCaptureInfo>> r;
        captures_info_.Visit([&](auto& v) {
            r.push_back(v);
        });
        return r;
    }

    std::vector<std::shared_ptr<pxrp::RpConnectedClientInfo>> PxStatistics::GetConnectedClientsInfo() {
        std::vector<std::shared_ptr<pxrp::RpConnectedClientInfo>> r;
        connected_clients_info_.Visit([&](auto& v) {
            r.push_back(v);
        });
        return r;
    }

    void PxStatistics::UpdateRelayAlive(const std::string& device_id, int64_t timestamp) {
        auto opt_ra = relays_alive_.TryGet(device_id);
        if (opt_ra.has_value()) {
            auto ra = opt_ra.value();
            ra->last_update_ts_ = timestamp;
        }
        else {
            auto ra = std::make_shared<PxStatRelayAlive>();
            ra->device_id_ = device_id;
            ra->created_ts_ = timestamp;
            ra->last_update_ts_ = timestamp;
            relays_alive_.Insert(device_id, ra);
        }
    }

    int64_t PxStatistics::GetRelayLastUpdateTimestamp(const std::string& device_id) {
        if (auto r = relays_alive_.TryGet(device_id); r.has_value()) {
            return r.value()->last_update_ts_;
        }
        return 0;
    }

}
