//
// Created RGAA on 15/11/2024.
//

#include "amf_encoder_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "video_encoder_vce.h"
#include "amf_encoder_defs.h"
#include "px_common_new/log.h"
#include "px_render/modules/module_ids.h"


namespace px
{

    std::string AmfEncoderPlugin::GetPluginId() {
        return kAmfEncoderPluginId;
    }

    std::string AmfEncoderPlugin::GetPluginName() {
        return kAmfPluginName;
    }

    std::string AmfEncoderPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t AmfEncoderPlugin::GetVersionCode() {
        return 110;
    }

    std::string AmfEncoderPlugin::GetPluginDescription() {
        return "AMD hardware encoder";
    }

    bool AmfEncoderPlugin::CanEncodeTexture() {
        return true;
    }

    void AmfEncoderPlugin::On1Second() {
        PxVideoEncoderPlugin::On1Second();
    }

    bool AmfEncoderPlugin::OnCreate(const px::PxPluginParam& param) {
        px::PxVideoEncoderPlugin::OnCreate(param);
        return true;
    }

    bool AmfEncoderPlugin::OnDestroy() {
        PxVideoEncoderPlugin::OnStop();
        ExitAll();
        return PxVideoEncoderPlugin::OnDestroy();
    }

    void AmfEncoderPlugin::InsertIdr() {
        PxVideoEncoderPlugin::InsertIdr();
        if (IsWorking()) {
            for (const auto& [monitor_name, video_encoder] : video_encoders_) {
                video_encoder->InsertIdr();
                LOGI("Insert IDR for : {}", monitor_name);
            }
        }
    }

    bool AmfEncoderPlugin::HasEncoderForMonitor(const std::string& monitor_name) {
        return video_encoders_.find(monitor_name) == video_encoders_.end();
    }

    bool AmfEncoderPlugin::IsWorking() {
        return plugin_enabled_ && !video_encoders_.empty();
    }

    bool AmfEncoderPlugin::Init(const EncoderConfig& config, const std::string& monitor_name) {
        if (!plugin_enabled_) {
            LOGE("This plugin is disabled!");
            return false;
        }
        PxVideoEncoderPlugin::Init(config, monitor_name);
        auto encoder = std::make_shared<VideoEncoderVCE>(this, config.adapter_uid_);
        auto ok = encoder->Initialize(config);
        if (!ok) {
            LOGE("AMF encoder init failed for: {}", monitor_name);
            return false;
        }
        video_encoders_[monitor_name] = encoder;
        LOGI("Video encoder init success for monitor: {}", monitor_name);
        return true;
    }

    VideoEncoderError AmfEncoderPlugin::Encode(
        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d,
        uint64_t frame_index,
        const CaptureVideoFrame& capture_frame) {
        if (IsWorking()) {
            auto monitor_name = std::string(capture_frame.display_name_);
            if (video_encoders_.find(monitor_name) == video_encoders_.end()) {
                LOGE("Not found video encoder for monitor: {}", monitor_name);
                return VideoEncoderError::NotFound();
            }
            auto video_encoder = video_encoders_[monitor_name];
            if (!video_encoder->Encode(tex2d, frame_index, capture_frame)) {
                return VideoEncoderError::EncodeFailed();
            }
        }
        else {
            LOGI("Amf encoder is not working, ignore it.");
        }
        return VideoEncoderError::Ok();
    }

    void AmfEncoderPlugin::Exit(const std::string& monitor_name) {
        if (video_encoders_.find(monitor_name) != video_encoders_.end()) {
            video_encoders_[monitor_name]->Exit();
            video_encoders_.erase(monitor_name);
        }
    }

    void AmfEncoderPlugin::ExitAll() {
        for (const auto& [monitor, video_encoder] : video_encoders_) {
            if (video_encoder) {
                video_encoder->Exit();
            }
        }
        video_encoders_.clear();
        LOGI("Amf encoders all exit.");
    }

    std::map<std::string, WorkingEncoderInfoPtr> AmfEncoderPlugin::GetWorkingCapturesInfo() {
        std::map<std::string, WorkingEncoderInfoPtr> result;
        for (const auto& [monitor, video_encoder] : video_encoders_) {
            auto info = std::make_shared<WorkingEncoderInfo>();
            info->target_name_ = monitor;
            info->fps_ = video_encoder->GetEncodeFps();
            info->encoder_name_ = "AMF";
            info->encode_durations_ = video_encoder->GetEncodeDurations();
            result.insert({monitor, info});
        }
        return result;
    }

    std::optional<EncoderCapability> AmfEncoderPlugin::GetEncoderCapability(const std::string& monitor_name) {
        // to do 需要研究下A卡如何支持444编码
        EncoderCapability cap;
        cap.support_h264_yuv444_ = false;
        cap.support_hevc_yuv444_ = false;
        return { cap };
    }
}
