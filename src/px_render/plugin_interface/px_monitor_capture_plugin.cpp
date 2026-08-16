//
// Created by RGAA on 22/11/2024.
//

#include "px_monitor_capture_plugin.h"

namespace px
{
    PxMonitorCapturePlugin::PxMonitorCapturePlugin() {

    }

    bool PxMonitorCapturePlugin::OnCreate(const px::PxPluginParam &param) {
        PxPluginInterface::OnCreate(param);
        return true;
    }

    bool PxMonitorCapturePlugin::OnDestroy() {
        PxPluginInterface::OnDestroy();
        return true;
    }

    std::vector<CaptureMonitorInfo> PxMonitorCapturePlugin::GetCaptureMonitorInfo() {
        return {};
    }

    void PxMonitorCapturePlugin::SetCaptureMonitor(const std::string& name) {

    }

    std::string PxMonitorCapturePlugin::GetCapturingMonitor() {
        return capturing_monitor_name_;
    }

    void PxMonitorCapturePlugin::SetCaptureFps(int fps) {
        capture_fps_ = fps;
    }

    void PxMonitorCapturePlugin::On16MilliSecond() {

    }

    void PxMonitorCapturePlugin::On33MilliSecond() {

    }

    void PxMonitorCapturePlugin::SetCaptureErrorCallback(const px::CaptureErrorCallback& cbk) {
        capture_err_callback_ = cbk;
    }

}