//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_MOCK_VIDEO_STREAM_PLUGIN_H
#define PX_RENDER_MOCK_VIDEO_STREAM_PLUGIN_H

#include "px_render/plugin_interface/px_data_provider_plugin.h"

namespace px
{
    class MockVideoStreamRuntime;

    class MockVideoStreamPlugin : public PxDataProviderPlugin {
    public:
        MockVideoStreamPlugin() { plugin_enabled_ = false; }

        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const px::PxPluginParam& param) override;
        bool OnDestroy() override;
        void On1Second() override;
        void StartProviding() override;
        void StopProviding() override;

    private:
        std::shared_ptr<MockVideoStreamRuntime> runtime_;
    };

}


PX_PLUGIN_EXPORT(px::MockVideoStreamPlugin)


#endif //PX_UDP_PLUGIN_H
