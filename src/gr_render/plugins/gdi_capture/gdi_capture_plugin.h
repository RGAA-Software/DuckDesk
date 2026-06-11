
#pragma once

#include "gr_render/plugin_interface/gr_monitor_capture_plugin.h"
#include <map>
#include <string>
#include <memory>

namespace tc
{

    class PluginDesktopCapture;

    class GdiCapturePlugin : public GrMonitorCapturePlugin {
    public:
        GdiCapturePlugin();
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const tc::GrPluginParam& param) override;
        bool OnDestroy() override;
        bool TryInitSpecificCapture() override;
        bool StartCapturing() override;
        void StopCapturing() override;
        std::vector<CaptureMonitorInfo> GetCaptureMonitorInfo() override;
        std::string GetCapturingMonitorName() override;
        void SetCaptureMonitor(const std::string& name) override;
        void SetCaptureFps(int fps) override;
        void OnNewClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& conn_type) override;
        void On1Second() override;

        void DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) override;

        VirtualDesktopBoundRectangleInfo GetVirtualDesktopBoundRectangleInfo() override;

        //根据显示器名字获取排序位置
        std::optional<int> GetMonIndexByName(const std::string& name) override;

        // Capturing target <==> information
        std::map<std::string, WorkingCaptureInfoPtr> GetWorkingCapturesInfo() override;

        void CreateCaptures();
        void HandleDisplayDeviceChangeEvent() override;
        void RestartCapturing();
        std::vector<SupportedResolution> GetSupportedResolutions(const std::wstring& name);
        void CalculateVirtualDeskInfo();
        void NotifyCaptureMonitorInfo();
        bool ExistCaptureMonitor(const std::string& name);

    public:
        std::map<std::string, CaptureMonitorInfo> monitors_;

    private:
        std::map<std::string, int> previewers_;
        std::map<std::string, std::shared_ptr<PluginDesktopCapture>> captures_;
        std::vector<CaptureMonitorInfo> sorted_monitors_;
        VirtualDesktopBoundRectangleInfo virtual_desktop_bound_rectangle_info_;
    };


}





