
#pragma once

#include "px_render/architecture/sources/monitor_capture_source.h"
#include <map>
#include <string>
#include <memory>
#include <mutex>

namespace px
{

    class DesktopCaptureSource;

    class GdiCaptureSource : public MonitorCaptureSource {
    public:
        GdiCaptureSource();
        std::string Id() const override;
        std::string Name() const override;
        std::string VersionName() const override;
        uint32_t VersionCode() const override;
        std::string Description() const override;
        bool Start(const px::RenderModuleConfiguration& param) override;
        bool Destroy() override;
        bool InitializeCapture() override;
        bool StartCapturing() override;
        void StopCapturing() override;
        std::vector<CaptureMonitorInfo> CaptureMonitors() const override;
        std::string CapturingMonitorName() const override;
        void SelectMonitor(const std::string& name) override;
        void SetCaptureFps(int fps) override;
        void OnClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& conn_type) override;
        void Tick1Second() override;

        void HandleAppEvent(const std::shared_ptr<AppBaseEvent>& event) override;

        VirtualDesktopBoundRectangleInfo VirtualDesktopBounds() const override;

        //根据显示器名字获取排序位置
        std::optional<int> MonitorIndexByName(const std::string& name) const override;

        // Capturing target <==> information
        std::map<std::string, WorkingCaptureInfoPtr> WorkingCaptures() const override;

        void CreateCaptures();
        void HandleDisplayDeviceChange() override;
        void RestartCapturing();
        std::vector<SupportedResolution> GetSupportedResolutions(const std::wstring& name);
        void CalculateVirtualDeskInfo();
        void NotifyCaptureMonitorInfo();
        bool ExistCaptureMonitor(const std::string& name);

    public:
        std::map<std::string, CaptureMonitorInfo> monitors_;

    private:
        std::map<std::string, int> previewers_;
        std::map<std::string, std::shared_ptr<DesktopCaptureSource>> captures_;
        std::vector<CaptureMonitorInfo> sorted_monitors_;
        VirtualDesktopBoundRectangleInfo virtual_desktop_bound_rectangle_info_;
        std::recursive_mutex capture_control_mutex_;
    };

}
