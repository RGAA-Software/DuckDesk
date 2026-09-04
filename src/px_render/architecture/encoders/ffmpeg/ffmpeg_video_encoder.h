//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_FFMPEG_VIDEO_ENCODER_H
#define PX_RENDER_FFMPEG_VIDEO_ENCODER_H

#include "px_render/architecture/encoders/video_encoder_module.h"

namespace px
{

    class Data;
    class Image;
    class FFmpegEncoder;

    class FfmpegVideoEncoder : public VideoEncoderModule {
    public:

        std::string Id() const override;
        std::string Name() const override;
        std::string VersionName() const override;
        uint32_t VersionCode() const override;
        std::string Description() const override;

        void Tick1Second() override;

        bool Start(const px::RenderModuleConfiguration &param) override;
        bool Destroy() override;
        void RequestKeyFrame() override;
        void RequestKeyFrame(const std::string& mon_name) override;
        bool IsWorking() const override;

        bool HasEncoderForMonitor(const std::string& monitor_name) const override;
        bool CanEncodeTexture() const override;
        bool Initialize(const EncoderConfig& config, const std::string& monitor_name) override;
        VideoEncoderError Encode(
            const std::shared_ptr<Image>& i420_image,
            uint64_t frame_index,
            const CaptureVideoFrame& capture_frame) override;
        void Remove(const std::string& monitor_name) override;
        void RemoveAll() override;
        std::map<std::string, WorkingEncoderInfoPtr> WorkingCaptures() const override;
        std::optional<EncoderCapability> Capability(const std::string& monitor_name) const override;
        // 动态调整码率/帧率(WebRTC BWE 经 PxPluginReconfigure 事件随动):
        // 码率走 x264 节流重开,fps 走编码输入侧跳帧,均不改 delta 链连续性
        void Reconfigure(const std::string& mon_name, uint32_t bps, uint32_t fps) override;

        void DisableHardware();
        bool IsHardwareEnabled();

    private:
        std::map<std::string, std::shared_ptr<FFmpegEncoder>> video_encoders_;
        bool hardware_enabled_ = true;
    };

}



#endif  // PX_RENDER_FFMPEG_VIDEO_ENCODER_H
