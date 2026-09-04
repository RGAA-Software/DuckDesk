//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_AMF_VIDEO_ENCODER_H
#define PX_RENDER_AMF_VIDEO_ENCODER_H

#include "px_render/architecture/encoders/video_encoder_module.h"
#include "px_render/modules/module_ids.h"

namespace px
{

    class VideoEncoderVCE;

    class AmfVideoEncoder : public VideoEncoderModule {
    public:

        std::string Id() const override;
        std::string Name() const override;
        std::string VersionName() const override;
        uint32_t VersionCode() const override;
        std::string Description() const override;

        bool CanEncodeTexture() const override;
        void Tick1Second() override;
        bool Start(const px::RenderModuleConfiguration &param) override;
        bool Destroy() override;
        void RequestKeyFrame() override;
        bool IsWorking() const override;

        bool HasEncoderForMonitor(const std::string& monitor_name) const override;
        bool Initialize(const EncoderConfig& config, const std::string& monitor_name) override;
        VideoEncoderError Encode(
            const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d,
            uint64_t frame_index,
            const CaptureVideoFrame& capture_frame) override;
        void Remove(const std::string& monitor_name) override;
        void RemoveAll() override;
        std::map<std::string, WorkingEncoderInfoPtr> WorkingCaptures() const override;

        std::optional<EncoderCapability> Capability(const std::string& monitor_name) const override;
    private:
        std::map<std::string, std::shared_ptr<VideoEncoderVCE>> video_encoders_;

    };

}


#endif  // PX_RENDER_AMF_VIDEO_ENCODER_H
