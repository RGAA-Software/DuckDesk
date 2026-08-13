//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_NVENC_ENCODER_PLUGIN_H
#define GAMMARAY_NVENC_ENCODER_PLUGIN_H

#include "gr_render/plugin_interface/gr_video_encoder_plugin.h"

namespace tc
{

    class NVENCVideoEncoder;

    class NvencEncoderPlugin : public GrVideoEncoderPlugin {
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


#endif //GAMMARAY_UDP_PLUGIN_H
