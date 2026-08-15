//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_MEDIA_RECORDER_PLUGIN_H
#define GAMMARAY_MEDIA_RECORDER_PLUGIN_H

#include "px_render/plugin_interface/px_video_encoder_plugin.h"

namespace tc
{

    class Data;
    class Image;
    class FFmpegEncoder;

    class FFmpegEncoderPlugin : public GrVideoEncoderPlugin {
    public:

        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;

        void On1Second() override;

        bool OnCreate(const tc::GrPluginParam &param) override;
        bool OnDestroy() override;
        void InsertIdr() override;
        void InsertIdr(const std::string& mon_name) override;
        bool IsWorking() override;

        bool HasEncoderForMonitor(const std::string& monitor_name) override;
        bool CanEncodeTexture() override;
        bool Init(const EncoderConfig& config, const std::string& monitor_name) override;
        VideoEncoderError Encode(const std::shared_ptr<Image>& i420_image, uint64_t frame_index, const std::any& extra) override;
        void Exit(const std::string& monitor_name) override;
        void ExitAll() override;
        std::map<std::string, WorkingEncoderInfoPtr> GetWorkingCapturesInfo() override;
        std::optional<EncoderCapability> GetEncoderCapability(const std::string& monitor_name) override;
        // 动态调整码率/帧率(WebRTC BWE 经 GrPluginConfigEncoder 事件随动):
        // 码率走 x264 节流重开,fps 走编码输入侧跳帧,均不改 delta 链连续性
        void ConfigEncoder(const std::string& mon_name, uint32_t bps, uint32_t fps) override;

        void DisableHardware();
        bool IsHardwareEnabled();

    private:
        std::map<std::string, std::shared_ptr<FFmpegEncoder>> video_encoders_;
        bool hardware_enabled_ = true;
    };

}



#endif //GAMMARAY_UDP_PLUGIN_H
