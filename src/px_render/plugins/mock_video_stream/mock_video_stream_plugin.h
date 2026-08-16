//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_MOCK_VIDEO_STREAM_PLUGIN_H
#define PX_RENDER_MOCK_VIDEO_STREAM_PLUGIN_H

#include "px_render/plugin_interface/px_data_provider_plugin.h"
#include <opencv2/opencv.hpp>

namespace px
{

    class MockVideoStreamPlugin : public PxDataProviderPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const px::PxPluginParam& param) override;
        void On1Second() override;
        void StartProviding() override;
        void StopProviding() override;

    private:
        void ReGenerate();

    private:
        cv::Mat mock_image_;
        int width_ = 640;
        int height_ = 480;
        uint64_t frame_index_ = 0;
    };

}


PX_PLUGIN_EXPORT(px::MockVideoStreamPlugin)


#endif //PX_UDP_PLUGIN_H
