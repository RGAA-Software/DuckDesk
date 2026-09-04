#include "monitor_capture_source.h"

namespace px {

bool MonitorCaptureSource::Start(
    const RenderModuleConfiguration& configuration) {
    return RenderModule::Start(configuration);
}

bool MonitorCaptureSource::Destroy() { return RenderModule::Destroy(); }

void MonitorCaptureSource::SetCaptureErrorCallback(
    const CaptureErrorCallback& callback) {
    capture_error_callback_ = callback;
}

std::vector<CaptureMonitorInfo> MonitorCaptureSource::CaptureMonitors() const {
    return {};
}

VirtualDesktopBoundRectangleInfo
MonitorCaptureSource::VirtualDesktopBounds() const {
    return {};
}

void MonitorCaptureSource::SelectMonitor(const std::string& name) {
    selected_monitor_name_ = name;
}

std::string MonitorCaptureSource::SelectedMonitor() const {
    return selected_monitor_name_;
}

void MonitorCaptureSource::SetCaptureFps(const int fps) { capture_fps_ = fps; }
void MonitorCaptureSource::Tick16Milliseconds() {}
void MonitorCaptureSource::Tick33Milliseconds() {}

}  // namespace px
