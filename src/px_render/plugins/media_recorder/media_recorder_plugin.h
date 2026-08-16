//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_MEDIA_RECORDER_PLUGIN_H
#define PX_RENDER_MEDIA_RECORDER_PLUGIN_H

#include "px_render/plugin_interface/px_stream_plugin.h"

namespace px
{

    class MediaRecorderPlugin : public PxStreamPlugin {
    public:
        MediaRecorderPlugin() { plugin_enabled_ = false; }

        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;

    };

}


PX_PLUGIN_EXPORT(px::MediaRecorderPlugin)


#endif //PX_UDP_PLUGIN_H
