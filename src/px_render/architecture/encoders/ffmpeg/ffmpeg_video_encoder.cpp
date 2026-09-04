//
// Created RGAA on 15/11/2024.
//

#include "ffmpeg_video_encoder.h"
#include "px_render/plugin_interface/px_plugin_events.h"

#include <libyuv.h>

#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/image.h"
#include "px_common_new/win32/d3d_debug_helper.h"
#include "px_common_new/file.h"
#include "px_common_new/time_util.h"
#include "px_common_new/defer.h"
#include "px_render/modules/module_ids.h"
#include "ffmpeg_encoder.h"
#include <Winerror.h>


namespace px
{

    std::string FfmpegVideoEncoder::Id() const {
        return kFfmpegVideoEncoderId;
    }

    std::string FfmpegVideoEncoder::Name() const {
        return kFfmpegEncoderName;
    }

    std::string FfmpegVideoEncoder::VersionName() const {
        return "1.2.0";
    }

    uint32_t FfmpegVideoEncoder::VersionCode() const {
        return 120;
    }

    std::string FfmpegVideoEncoder::Description() const {
        return "Common software/hardware encoders";
    }

    void FfmpegVideoEncoder::Tick1Second() {
        VideoEncoderModule::Tick1Second();
    }

    bool FfmpegVideoEncoder::Start(const px::RenderModuleConfiguration& configuration) {
        px::VideoEncoderModule::Start(configuration);

        return true;
    }

    bool FfmpegVideoEncoder::Destroy() {
        VideoEncoderModule::Stop();
        RemoveAll();
        return VideoEncoderModule::Destroy();
    }

    void FfmpegVideoEncoder::RequestKeyFrame() {
        VideoEncoderModule::RequestKeyFrame();
        for (const auto& [monitor_index, video_encoder] : video_encoders_) {
            video_encoder->InsertIdr();
        }
    }

    void FfmpegVideoEncoder::RequestKeyFrame(const std::string& mon_name) {
        if (mon_name.empty()) {
            RequestKeyFrame();
            return;
        }
        // 只给目标屏补 IDR,其它屏的 delta 链不动(RTC 多 track 按屏定向)
        auto it = video_encoders_.find(mon_name);
        if (it != video_encoders_.end() && it->second) {
            it->second->InsertIdr();
        }
    }

    bool FfmpegVideoEncoder::IsWorking() const {
        return !video_encoders_.empty() && enabled_.load();
    }

    bool FfmpegVideoEncoder::CanEncodeTexture() const {
        return false;
    }

    bool FfmpegVideoEncoder::HasEncoderForMonitor(const std::string& monitor_name) const {
        return video_encoders_.find(monitor_name) != video_encoders_.end();
    }

    bool FfmpegVideoEncoder::Initialize(const EncoderConfig& config, const std::string& monitor_name) {
        if (!enabled_.load()) {
            LOGE("event=encoder.initialize component=ffmpeg outcome=rejected reason=disabled");
            return false;
        }
        VideoEncoderModule::Initialize(config, monitor_name);
        const auto owner = std::dynamic_pointer_cast<FfmpegVideoEncoder>(
            shared_from_this());
        auto encoder = std::make_shared<FFmpegEncoder>(owner);
        auto ok = encoder->Init(config, monitor_name);
        if (!ok) {
            LOGE("Initialize ffmpeg encoder for: {} failed.", monitor_name);
            return false;
        }
        LOGI("FFmpeg encoder init success for: {}", monitor_name);
        video_encoders_[monitor_name] = encoder;
        return true;
    }

    VideoEncoderError FfmpegVideoEncoder::Encode(
        const std::shared_ptr<Image>& i420_image,
        uint64_t frame_index,
        const CaptureVideoFrame& capture_frame) {
        auto monitor_name = std::string(capture_frame.display_name_);
        if (!i420_image) {
            return VideoEncoderError::InvalidInput();
        }
        if (!HasEncoderForMonitor(monitor_name)) {
            return VideoEncoderError::NotFound();
        }
        if (!video_encoders_[monitor_name]->Encode(
                i420_image, frame_index, capture_frame)) {
            return VideoEncoderError::EncodeFailed();
        }
        return VideoEncoderError::Ok();
    }

    void FfmpegVideoEncoder::Remove(const std::string& monitor_name) {
        if (HasEncoderForMonitor(monitor_name)) {
            video_encoders_[monitor_name]->Exit();
            video_encoders_.erase(monitor_name);
        }
    }

    void FfmpegVideoEncoder::RemoveAll() {
        for (const auto& [monitor, video_encoder] : video_encoders_) {
            video_encoder->Exit();
        }
        video_encoders_.clear();
    }

    std::map<std::string, WorkingEncoderInfoPtr> FfmpegVideoEncoder::WorkingCaptures() const {
        std::map<std::string, WorkingEncoderInfoPtr> result;
        for (const auto& [monitor, video_encoder] : video_encoders_) {
            result.insert({monitor, std::make_shared<WorkingEncoderInfo>(WorkingEncoderInfo {
                .target_name_ = monitor,
                .fps_ = video_encoder->GetEncodeFps(),
                .encoder_name_ = video_encoder->GetDisplayEncoderName(),
                .encode_durations_ = video_encoder->GetEncodeDurations(),
            })});
        }
        return result;
    }

    std::optional<EncoderCapability> FfmpegVideoEncoder::Capability(const std::string& monitor_name) const {
        /*
        Since it has been previously determined through the N-card or A-card SDK, 
        ffmpeg uses N-card or A-card hardware encoding and directly returns "yuv444 encoding output is not supported."
        */
        EncoderCapability cap;
        const auto found = video_encoders_.find(monitor_name);
        if (found != video_encoders_.end() && found->second) {
            auto encoder_config = found->second->GetEncoderConfig();
            if (EHardwareEncoder::kNvEnc == encoder_config.Hardware) {
                cap.support_h264_yuv444_ = false;
                cap.support_hevc_yuv444_ = false;
            }
            else if (EHardwareEncoder::kAmf == encoder_config.Hardware) {
                cap.support_h264_yuv444_ = false;
                cap.support_hevc_yuv444_ = false;
            }
            else if (EHardwareEncoder::kQsv == encoder_config.Hardware) {
                cap.support_h264_yuv444_ = false;
                cap.support_hevc_yuv444_ = false;
            }
            else if(EHardwareEncoder::kNone == encoder_config.Hardware) {
                cap.support_h264_yuv444_ = true;
                cap.support_hevc_yuv444_ = true;
            }
        }
        return { cap };
    }

    void FfmpegVideoEncoder::Reconfigure(const std::string& mon_name, uint32_t bps, uint32_t fps) {
        // mon_name 为空(WebRTC 侧事件不带屏名)时应用到全部编码器
        for (const auto& [monitor_index, video_encoder] : video_encoders_) {
            if (mon_name.empty() || monitor_index == mon_name) {
                video_encoder->SetTargetBitrate(bps);
                video_encoder->SetTargetFps(fps);
            }
        }
    }

    void FfmpegVideoEncoder::DisableHardware() {
        hardware_enabled_ = false;
    }

    bool FfmpegVideoEncoder::IsHardwareEnabled() {
        return hardware_enabled_;
    }
}
