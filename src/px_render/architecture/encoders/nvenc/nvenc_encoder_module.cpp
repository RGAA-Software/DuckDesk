//
// Created RGAA on 15/11/2024.
//

#include "nvenc_encoder_module.h"
#include "px_common/log.h"
#include "nvenc_encoder_defs.h"
#include "nvenc_video_encoder.h"
#include "px_render/modules/module_ids.h"
#include "px_render/architecture/events/render_event.h"
#include "px_render/architecture/runtime/render_execution_context.h"


namespace px
{
    std::string NvencEncoderModule::Id() const {
        return kNvencEncoderModuleId;
    }

    std::string NvencEncoderModule::Name() const {
        return kNvencEncoderName;
    }

    std::string NvencEncoderModule::VersionName() const {
        return "1.1.0";
    }

    uint32_t NvencEncoderModule::VersionCode() const {
        return 110;
    }

    std::string NvencEncoderModule::Description() const {
        return "Nvidia hardware encoder";
    }

    void NvencEncoderModule::Tick1Second() {
        VideoEncoderModule::Tick1Second();
    }

    bool NvencEncoderModule::Start(const px::RenderModuleConfiguration& param) {
        VideoEncoderModule::Start(param);
        return true;
    }

    bool NvencEncoderModule::Destroy() {
        VideoEncoderModule::Stop();
        RemoveAll();
        return VideoEncoderModule::Destroy();
    }

    void NvencEncoderModule::RequestKeyFrame() {
        VideoEncoderModule::RequestKeyFrame();
        if (IsWorking()) {
            for (const auto& [monitor_index, video_encoder] : video_encoders_) {
                video_encoder->InsertIdr();
            }
        }
    }

    void NvencEncoderModule::RequestKeyFrame(const std::string& mon_name) {
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

    bool NvencEncoderModule::InvalidateReferenceFrame(const std::string& mon_name, uint64_t invalid_frame_index) {
        bool accepted = false;
        if (mon_name.empty()) {
            for (const auto& [_, encoder] : video_encoders_) {
                if (encoder) {
                    accepted |= encoder->InvalidateRefFrame(invalid_frame_index);
                }
            }
            return accepted;
        }
        auto it = video_encoders_.find(mon_name);
        if (it != video_encoders_.end() && it->second) {
            accepted = it->second->InvalidateRefFrame(invalid_frame_index);
        }
        return accepted;
    }

    bool NvencEncoderModule::IsWorking() const {
        return enabled_.load() && !video_encoders_.empty();
    }

    bool NvencEncoderModule::CanEncodeTexture() const {
        return true;
    }

    bool NvencEncoderModule::HasEncoderForMonitor(const std::string& monitor_name) const {
#if 0
        LOGW("HasEncoderForMonitor monitor_name: {}", monitor_name);

        for (auto item : video_encoders_) {
            LOGW("HasEncoderForMonitor item: {}", item.first);
        }
#endif
        return video_encoders_.find(monitor_name) != video_encoders_.end();
    }

    bool NvencEncoderModule::Initialize(const EncoderConfig& config, const std::string& monitor_name) {
        if (!enabled_.load()) {
            LOGE("event=encoder.initialize component=nvenc outcome=rejected reason=disabled");
            return false;
        }
        VideoEncoderModule::Initialize(config, monitor_name);
        const auto owner = std::dynamic_pointer_cast<NvencEncoderModule>(
            shared_from_this());
        auto encoder = std::make_shared<NVENCVideoEncoder>(owner, config.adapter_uid_);
        LOGI("config bitrate: {} for monitor: {}", config.bitrate, monitor_name);
        auto ok = encoder->Initialize(config);
        if (!ok) {
            LOGE("Initialize NVENC encoder failed for monitor: {}", monitor_name);
            return false;
        }
        video_encoders_[monitor_name] = encoder;
        return ok;
    }

    VideoEncoderError NvencEncoderModule::Encode(
        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d,
        uint64_t frame_index,
        const CaptureVideoFrame& capture_frame) {
        auto monitor_name = std::string(capture_frame.display_name_);
        if (!HasEncoderForMonitor(monitor_name)) {
            return VideoEncoderError::NotFound();
        }
        if (!video_encoders_[monitor_name]->Encode(
                tex2d, frame_index, capture_frame)) {
            return VideoEncoderError::EncodeFailed();
        }
        return VideoEncoderError::Ok();
    }

    void NvencEncoderModule::Remove(const std::string& monitor_name) {
        if (video_encoders_.find(monitor_name) != video_encoders_.end()) {
            video_encoders_[monitor_name]->Exit();
            video_encoders_.erase(monitor_name);
            LOGW("Remove encoder for monitor_name: {}", monitor_name);
        }
    }

    void NvencEncoderModule::RemoveAll() {

    }

    std::map<std::string, WorkingEncoderInfoPtr> NvencEncoderModule::WorkingCaptures() const {
        std::map<std::string, WorkingEncoderInfoPtr> result;
        for (const auto& [name, encoder] : video_encoders_) {
            result.insert({name, std::make_shared<WorkingEncoderInfo>(WorkingEncoderInfo {
                .target_name_ = name,
                .fps_ = encoder->GetEncodeFps(),
                .encoder_name_ = "NVENC",
                .encode_durations_ = encoder->GetEncodeDurations(),
            })});
        }
        return result;
    }

    void NvencEncoderModule::Reconfigure(const std::string& mon_name, uint32_t bps, uint32_t fps) {
        if (bps == 0 || fps == 0) {
            return;
        }
        // mon_name 为空时给所有屏挂 pending;有名字时只动目标屏。
        // 实际 Reconfigure 延迟到各 encoder 的 Encode 路径,避免与 EncodeFrame 跨线程并发。
        for (const auto& [mon, encoder] : video_encoders_) {
            if (!encoder) {
                continue;
            }
            if (!mon_name.empty() && mon != mon_name) {
                continue;
            }
            encoder->Config(bps, fps);
        }
    }

    std::optional<EncoderCapability> NvencEncoderModule::Capability(const std::string& monitor_name) const {
        const auto found = video_encoders_.find(monitor_name);
        const auto encoder = found == video_encoders_.end() ? nullptr : found->second;
        if (!encoder) {
            return std::nullopt;
        }

        EncoderCapability cap;
        cap.support_h264_yuv444_ = encoder->SupportH264Yuv444();
        cap.support_hevc_yuv444_ = encoder->SupportHevcYuv444();

        return {cap};
    }
}
