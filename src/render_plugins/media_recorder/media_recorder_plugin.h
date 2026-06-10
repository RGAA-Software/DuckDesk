//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_MEDIA_RECORDER_PLUGIN_H
#define GAMMARAY_MEDIA_RECORDER_PLUGIN_H

#include "plugin_interface/gr_stream_plugin.h"

namespace tc
{

    class MediaRecorderPlugin : public GrStreamPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;

    };

}


GR_PLUGIN_EXPORT(tc::MediaRecorderPlugin)


#endif //GAMMARAY_UDP_PLUGIN_H
