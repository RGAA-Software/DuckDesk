//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_MEDIA_RECORDER_PLUGIN_H
#define GAMMARAY_MEDIA_RECORDER_PLUGIN_H

#include "px_render/plugin_interface/px_stream_plugin.h"
#include <map>

namespace tc
{

    class ObjDetectorPlugin : public GrStreamPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const tc::GrPluginParam& param) override;
        void On1Second() override;
        void OnRawVideoFrameRgba(const std::string& name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) override;
        void OnRawVideoFrameYuv(const std::string& name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) override;

    private:
        std::map<std::string, int> previewers_;

    };

}


GR_PLUGIN_EXPORT(tc::ObjDetectorPlugin)


#endif //GAMMARAY_UDP_PLUGIN_H
