//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_AMF_ENCODER_PLUGIN_H
#define PX_RENDER_AMF_ENCODER_PLUGIN_H

#include "px_render/plugin_interface/px_video_encoder_plugin.h"
#include "px_render/modules/module_ids.h"

namespace px
{

    class VideoEncoderVCE;

    class AmfEncoderPlugin : public PxVideoEncoderPlugin {
    public:

        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;

        bool CanEncodeTexture() override;
        void On1Second() override;
        bool OnCreate(const px::PxPluginParam &param) override;
        bool OnDestroy() override;
        void InsertIdr() override;
        bool IsWorking() override;

        bool HasEncoderForMonitor(const std::string& monitor_name) override;
        bool Init(const EncoderConfig& config, const std::string& monitor_name) override;
        VideoEncoderError Encode(
            const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d,
            uint64_t frame_index,
            const CaptureVideoFrame& capture_frame) override;
        void Exit(const std::string& monitor_name) override;
        void ExitAll() override;
        std::map<std::string, WorkingEncoderInfoPtr> GetWorkingCapturesInfo() override;

        std::optional<EncoderCapability> GetEncoderCapability(const std::string& monitor_name) override;
    private:
        std::map<std::string, std::shared_ptr<VideoEncoderVCE>> video_encoders_;

    };

}


#endif //PX_UDP_PLUGIN_H
