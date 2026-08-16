//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_NVENC_ENCODER_PLUGIN_H
#define PX_NVENC_ENCODER_PLUGIN_H

#include "px_render/plugin_interface/px_video_encoder_plugin.h"

namespace px
{

    class NVENCVideoEncoder;

    class NvencEncoderPlugin : public PxVideoEncoderPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;
        bool OnCreate(const px::PxPluginParam &param) override;
        bool OnDestroy() override;
        void InsertIdr() override;
        void InsertIdr(const std::string& mon_name) override;
        bool InvalidateRefFrame(const std::string& mon_name, uint64_t invalid_frame_index) override;
        bool IsWorking() override;

        bool HasEncoderForMonitor(const std::string& monitor_name) override;
        bool CanEncodeTexture() override;
        bool Init(const EncoderConfig& config, const std::string& monitor_name) override;
        VideoEncoderError Encode(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex2d, uint64_t frame_index, const std::any& extra) override;
        void Exit(const std::string& monitor_name) override;
        void ExitAll() override;
        std::map<std::string, WorkingEncoderInfoPtr> GetWorkingCapturesInfo() override;
        void ConfigEncoder(const std::string& mon_name, uint32_t bps, uint32_t fps) override;

        std::optional<EncoderCapability> GetEncoderCapability(const std::string& monitor_name) override;
    private:
        std::map<std::string, std::shared_ptr<NVENCVideoEncoder>> video_encoders_;
    };

}


#endif //PX_UDP_PLUGIN_H
