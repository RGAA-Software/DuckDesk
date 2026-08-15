//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_FRAME_CARRIER_PLUGIN_H
#define GAMMARAY_FRAME_CARRIER_PLUGIN_H

#include "px_render/plugin_interface/px_frame_carrier_plugin.h"

namespace px
{

    class File;
    class Image;
    class VideoFrameCarrier;
    class GrFrameProcessorPlugin;

    class FrameCarrierPlugin : public GrFrameCarrierPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;
        bool OnCreate(const px::GrPluginParam &param) override;
        bool OnDestroy() override;

        // init carrier
        bool InitFrameCarrier(const px::GrCarrierParams &params) override;

        // copy texture
        std::shared_ptr<GrCarriedFrame> CopyTexture(const std::string& mon_name, uint64_t handle, uint64_t frame_index) override;

        // Map Texture from GPU -> CPU
        bool MapRawTexture(const std::string& mon_name, const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture, DXGI_FORMAT format, int height,
                                   std::function<void(const std::shared_ptr<Image>&)>&& rgba_cbk,
                                   std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk) override;

        // RGBA -> YUV
        bool ConvertRawImage(const std::string& mon_name, const std::shared_ptr<Image> image,
                                   std::function<void(const std::shared_ptr<Image>&)>&& rgba_cbk,
                                   std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk) override;

        // logo image
        std::shared_ptr<Image> GetLogoImage();

        // logo points
        std::vector<std::pair<int, int>> GetLogoPoints();
        // big log points
        std::vector<std::pair<int, int>> GetBigLogoPoints();

        // cover points
        std::vector<std::pair<int, int>> GetCoverPoints();

        int GetAuthRole() const;

    private:
        std::shared_ptr<VideoFrameCarrier> GetFrameCarrier(const std::string& monitor_name);
        void ChangeLogoPosition();

    private:
        // monitor name <=> Frame carrier
        std::map<std::string, std::shared_ptr<VideoFrameCarrier>> frame_carriers_;
        // logo image item
        std::shared_ptr<Image> logo_image_ = nullptr;
        // logo points
        std::vector<std::pair<int, int>> logo_points_;
        // big log points
        std::vector<std::pair<int, int>> big_logo_points_;
        // full screen
        std::vector<std::pair<int, int>> cover_points_;
        int timer_count_ = 0;
    };

}



#endif //GAMMARAY_UDP_PLUGIN_H
