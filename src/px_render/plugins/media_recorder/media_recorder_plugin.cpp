//
// Created RGAA on 15/11/2024.
//

#include "media_recorder_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugins/plugin_ids.h"

namespace px
{
    std::string MediaRecorderPlugin::GetPluginId() {
        return kMediaRecorderPluginId;
    }

    std::string MediaRecorderPlugin::GetPluginName() {
        return "Media Recorder(Server)";
    }

    std::string MediaRecorderPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t MediaRecorderPlugin::GetVersionCode() {
        return 110;
    }

    void MediaRecorderPlugin::On1Second() {

    }

    std::string MediaRecorderPlugin::GetPluginDescription() {
        return "Media recorder in server side";
    }

}
