//
// Created RGAA on 15/11/2024.
//

#include "obj_detector_plugin.h"
#include "px_render/plugin_interface/gr_plugin_events.h"
#include "px_common_new/log.h"
#include "px_common_new/file.h"
#include "px_common_new/image.h"
#include "px_render/plugins/plugin_ids.h"

namespace tc
{

    std::string ObjDetectorPlugin::GetPluginId() {
        return kNetObjDetectorPluginId;
    }

    std::string ObjDetectorPlugin::GetPluginName() {
        return "Obj Detector";
    }

    std::string ObjDetectorPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t ObjDetectorPlugin::GetVersionCode() {
        return 110;
    }

    std::string ObjDetectorPlugin::GetPluginDescription() {
        return "Object detector";
    }

    void ObjDetectorPlugin::On1Second() {
        GrPluginInterface::On1Second();

    }
    
    bool ObjDetectorPlugin::OnCreate(const tc::GrPluginParam &param) {
        GrPluginInterface::OnCreate(param);
        plugin_type_ = GrPluginType::kStream;

        if (!IsPluginEnabled()) {
            return true;
        }
        return true;
    }

    void ObjDetectorPlugin::OnRawVideoFrameRgba(const std::string& name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) {
        if (!IsPluginEnabled()) {
            return;
        }
        LOGI("ObjDetector: name={}, idx={}, size={}x{}", name, frame_idx, frame_width, frame_height);
    }

    void ObjDetectorPlugin::OnRawVideoFrameYuv(const std::string& name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) {

    }

}
