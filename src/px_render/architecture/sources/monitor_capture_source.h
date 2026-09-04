#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "architecture/modules/render_module.h"
#include "architecture/sources/capture_types.h"
#include "px_capture_new/monitor_util.h"
#include "px_render/plugin_interface/px_monitor_capture_error.h"

namespace px {

struct WorkingCaptureInfo final {
    std::string target_name_;
    std::int32_t fps_{0};
    std::string capture_type_;
    std::int32_t capture_frame_width_{0};
    std::int32_t capture_frame_height_{0};
    std::vector<std::int32_t> capture_gaps_;
};
using WorkingCaptureInfoPtr = std::shared_ptr<WorkingCaptureInfo>;

class MonitorCaptureSource : public RenderModule {
public:
    [[nodiscard]] RenderModuleKind Kind() const final {
        return RenderModuleKind::kSource;
    }

    bool Start(const RenderModuleConfiguration& configuration) override;
    bool Destroy() override;
    void SetCaptureErrorCallback(const CaptureErrorCallback& callback);

    virtual bool InitializeCapture() = 0;
    virtual bool StartCapturing() = 0;
    virtual void StopCapturing() = 0;
    [[nodiscard]] virtual std::vector<CaptureMonitorInfo> CaptureMonitors() const;
    [[nodiscard]] virtual VirtualDesktopBoundRectangleInfo VirtualDesktopBounds() const;
    virtual void SelectMonitor(const std::string& name);
    [[nodiscard]] std::string SelectedMonitor() const;
    [[nodiscard]] virtual std::map<std::string, WorkingCaptureInfoPtr>
    WorkingCaptures() const = 0;
    virtual void SetCaptureFps(int fps);
    [[nodiscard]] virtual std::string CapturingMonitorName() const = 0;
    [[nodiscard]] virtual std::optional<int> MonitorIndexByName(
        const std::string& name) const = 0;
    virtual void HandleDisplayDeviceChange() = 0;
    virtual void Tick16Milliseconds();
    virtual void Tick33Milliseconds();

    [[nodiscard]] static bool IsValidRect(const RECT& rectangle) noexcept {
        return rectangle.right > rectangle.left &&
            rectangle.bottom > rectangle.top;
    }

protected:
    int capture_fps_{60};
    std::string selected_monitor_name_;
    CaptureErrorCallback capture_error_callback_;
};

}  // namespace px
