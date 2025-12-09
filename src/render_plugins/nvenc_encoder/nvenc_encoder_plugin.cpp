//
// Created RGAA on 15/11/2024.
//

#include "nvenc_encoder_plugin.h"
#include "tc_common_new/log.h"
#include "nvenc_encoder_defs.h"
#include "nvenc_video_encoder.h"
#include "tc_common_new/memory_stat.h"
#include "render/plugins/plugin_ids.h"
#include "plugin_interface/gr_plugin_events.h"
#include "plugin_interface/gr_plugin_context.h"

static void* GetInstance() {
    static tc::NvencEncoderPlugin plugin;
    return (void*)&plugin;
}

namespace tc
{
    std::string NvencEncoderPlugin::GetPluginId() {
        return kNvencEncoderPluginId;
    }

    std::string NvencEncoderPlugin::GetPluginName() {
        return kNvencPluginName;
    }

    std::string NvencEncoderPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t NvencEncoderPlugin::GetVersionCode() {
        return 110;
    }

    std::string NvencEncoderPlugin::GetPluginDescription() {
        return "Nvidia hardware encoder";
    }

    void NvencEncoderPlugin::On1Second() {
        GrVideoEncoderPlugin::On1Second();
#if MEMORY_STST_ON
        plugin_context_->PostWorkTask([=, this]() {
            auto info = MemoryStat::Instance()->GetStatInfo();
            LOGI("Memory usage: {}", info.Dump());
        });
#endif
    }

    bool NvencEncoderPlugin::OnCreate(const tc::GrPluginParam& param) {
        GrVideoEncoderPlugin::OnCreate(param);
        return true;
    }

    bool NvencEncoderPlugin::OnDestroy() {
        GrVideoEncoderPlugin::OnDestroy();
        return true;
    }

    void NvencEncoderPlugin::InsertIdr() {
        GrVideoEncoderPlugin::InsertIdr();
        if (IsWorking()) {
            for (const auto& [monitor_index, video_encoder] : video_encoders_) {
                video_encoder->InsertIdr();
            }
        }
    }

    bool NvencEncoderPlugin::IsWorking() {
        return plugin_enabled_ && !video_encoders_.empty();
    }

    bool NvencEncoderPlugin::CanEncodeTexture() {
        return true;
    }

    bool NvencEncoderPlugin::HasEncoderForMonitor(const std::string& monitor_name) {
#if 0
        LOGW("HasEncoderForMonitor monitor_name: {}", monitor_name);

        for (auto item : video_encoders_) {
            LOGW("HasEncoderForMonitor item: {}", item.first);
        }
#endif
        return video_encoders_.find(monitor_name) != video_encoders_.end();
    }

    bool NvencEncoderPlugin::Init(const EncoderConfig& config, const std::string& monitor_name) {
        if (!plugin_enabled_) {
            LOGE("This plugin is disabled!");
            return false;
        }
        GrVideoEncoderPlugin::Init(config, monitor_name);
        auto encoder = std::make_shared<NVENCVideoEncoder>(this, config.adapter_uid_);
        LOGI("config bitrate: {} for monitor: {}", config.bitrate, monitor_name);
        auto ok = encoder->Initialize(config);
        if (!ok) {
            LOGE("Init NVENC encoder failed for monitor: {}", monitor_name);
            return false;
        }
        video_encoders_[monitor_name] = encoder;
        return ok;
    }

    VideoEncoderError NvencEncoderPlugin::Encode(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d, uint64_t frame_index, const std::any& extra) {
        auto cap_video_msg = std::any_cast<CaptureVideoFrame>(extra);
        auto monitor_name = std::string(cap_video_msg.display_name_);
        if (!HasEncoderForMonitor(monitor_name)) {
            return VideoEncoderError::NotFound();
        }
        if (!video_encoders_[monitor_name]->Encode(tex2d, frame_index, extra)) {
            return VideoEncoderError::EncodeFailed();
        }
        return VideoEncoderError::Ok();
    }

    void NvencEncoderPlugin::Exit(const std::string& monitor_name) {
        if (video_encoders_.find(monitor_name) != video_encoders_.end()) {
            video_encoders_[monitor_name]->Exit();
            video_encoders_.erase(monitor_name);
            LOGW("Exit encoder for monitor_name: {}", monitor_name);
        }
    }

    void NvencEncoderPlugin::ExitAll() {

    }

    std::map<std::string, WorkingEncoderInfoPtr> NvencEncoderPlugin::GetWorkingCapturesInfo() {
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

    void NvencEncoderPlugin::ConfigEncoder(const std::string& mon_name, uint32_t bps, uint32_t fps) {
        if (bps == 0 || fps == 0) {
            return;
        }
        // todo: target encoder == mon_name
        for (const auto& [mon, encoder] : video_encoders_) {
            encoder->Config(bps, fps);
        }
    }

    std::optional<EncoderCapability> NvencEncoderPlugin::GetEncoderCapability(const std::string& monitor_name) {
        auto encoder = video_encoders_[monitor_name];
        if (!encoder) {
            return std::nullopt;
        }

        EncoderCapability cap;
        cap.support_h264_yuv444_ = encoder->SupportH264Yuv444();
        cap.support_hevc_yuv444_ = encoder->SupportHevcYuv444();

        return {cap};
    }
}
