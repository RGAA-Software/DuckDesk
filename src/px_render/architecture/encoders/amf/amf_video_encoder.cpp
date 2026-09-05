//
// Created RGAA on 15/11/2024.
//

#include "amf_video_encoder.h"
#include "px_render/architecture/events/render_event.h"
#include "video_encoder_vce.h"
#include "amf_encoder_defs.h"
#include "px_common/log.h"
#include "px_render/modules/module_ids.h"


namespace px
{

    std::string AmfVideoEncoder::Id() const {
        return kAmfVideoEncoderId;
    }

    std::string AmfVideoEncoder::Name() const {
        return kAmfPluginName;
    }

    std::string AmfVideoEncoder::VersionName() const {
        return "1.1.0";
    }

    uint32_t AmfVideoEncoder::VersionCode() const {
        return 110;
    }

    std::string AmfVideoEncoder::Description() const {
        return "AMD hardware encoder";
    }

    bool AmfVideoEncoder::CanEncodeTexture() const {
        return true;
    }

    void AmfVideoEncoder::Tick1Second() {
        VideoEncoderModule::Tick1Second();
    }

    bool AmfVideoEncoder::Start(const px::RenderModuleConfiguration& param) {
        px::VideoEncoderModule::Start(param);
        return true;
    }

    bool AmfVideoEncoder::Destroy() {
        VideoEncoderModule::Stop();
        RemoveAll();
        return VideoEncoderModule::Destroy();
    }

    void AmfVideoEncoder::RequestKeyFrame() {
        VideoEncoderModule::RequestKeyFrame();
        if (IsWorking()) {
            for (const auto& [monitor_name, video_encoder] : video_encoders_) {
                video_encoder->InsertIdr();
                LOGI("Insert IDR for : {}", monitor_name);
            }
        }
    }

    bool AmfVideoEncoder::HasEncoderForMonitor(const std::string& monitor_name) const {
        return video_encoders_.find(monitor_name) == video_encoders_.end();
    }

    bool AmfVideoEncoder::IsWorking() const {
        return enabled_.load() && !video_encoders_.empty();
    }

    bool AmfVideoEncoder::Initialize(const EncoderConfig& config, const std::string& monitor_name) {
        if (!enabled_.load()) {
            LOGE("event=encoder.initialize component=amf outcome=rejected reason=disabled");
            return false;
        }
        VideoEncoderModule::Initialize(config, monitor_name);
        const auto owner = std::dynamic_pointer_cast<AmfVideoEncoder>(
            shared_from_this());
        auto encoder = std::make_shared<VideoEncoderVCE>(owner, config.adapter_uid_);
        auto ok = encoder->Initialize(config);
        if (!ok) {
            LOGE("AMF encoder init failed for: {}", monitor_name);
            return false;
        }
        video_encoders_[monitor_name] = encoder;
        LOGI("Video encoder init success for monitor: {}", monitor_name);
        return true;
    }

    VideoEncoderError AmfVideoEncoder::Encode(
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

    void AmfVideoEncoder::Remove(const std::string& monitor_name) {
        if (video_encoders_.find(monitor_name) != video_encoders_.end()) {
            video_encoders_[monitor_name]->Exit();
            video_encoders_.erase(monitor_name);
        }
    }

    void AmfVideoEncoder::RemoveAll() {
        for (const auto& [monitor, video_encoder] : video_encoders_) {
            if (video_encoder) {
                video_encoder->Exit();
            }
        }
        video_encoders_.clear();
        LOGI("Amf encoders all exit.");
    }

    std::map<std::string, WorkingEncoderInfoPtr> AmfVideoEncoder::WorkingCaptures() const {
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

    std::optional<EncoderCapability> AmfVideoEncoder::Capability(const std::string& monitor_name) const {
        // to do 需要研究下A卡如何支持444编码
        EncoderCapability cap;
        cap.support_h264_yuv444_ = false;
        cap.support_hevc_yuv444_ = false;
        return { cap };
    }
}
