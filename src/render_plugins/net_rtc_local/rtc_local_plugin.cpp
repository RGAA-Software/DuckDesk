//
// Created RGAA on 15/11/2024.
//

#include "rtc_local_plugin.h"
#include "plugin_interface/gr_plugin_events.h"
#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "tc_common_new/image.h"
#include "render/plugins/plugin_ids.h"

void* GetInstance() {
    static tc::RtcLocalPlugin plugin;
    return (void*)&plugin;
}

namespace tc
{

    std::string RtcLocalPlugin::GetPluginId() {
        return kNetRtcLocalPluginId;
    }

    std::string RtcLocalPlugin::GetPluginName() {
        return "RTC Local";
    }

    std::string RtcLocalPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t RtcLocalPlugin::GetVersionCode() {
        return 110;
    }

    std::string RtcLocalPlugin::GetPluginDescription() {
        return "RTC in local mode";
    }

    void RtcLocalPlugin::On1Second() {
        GrPluginInterface::On1Second();

    }
    
    bool RtcLocalPlugin::OnCreate(const tc::GrPluginParam &param) {
        GrPluginInterface::OnCreate(param);
        plugin_type_ = GrPluginType::kStream;

        if (!IsPluginEnabled()) {
            return true;
        }
        root_widget_->hide();
        //root_widget_->show();
        return true;
    }

    void RtcLocalPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {

    }

    bool RtcLocalPlugin::PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        return false;
    }

    bool RtcLocalPlugin::PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        return false;
    }


}
