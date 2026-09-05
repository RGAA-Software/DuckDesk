//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_DDA_CAPTURE_SOURCE_H
#define PX_DDA_CAPTURE_SOURCE_H
#include <functional>
#include <optional>
#include <mutex>
#include "px_render/architecture/sources/monitor_capture_source.h"
#include "px_common/concurrent_hashmap.h"

namespace px
{

    class Thread;
    class CursorCapture;
    class DesktopCaptureSource;

    class DdaCaptureSource : public MonitorCaptureSource {
    public:
        DdaCaptureSource();
        std::string Id() const override;
        std::string Name() const override;
        std::string VersionName() const override;
        uint32_t VersionCode() const override;
        std::string Description() const override;
        bool IsWorking() const override;
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
        void Tick16Milliseconds() override;
        void Tick33Milliseconds() override;

        //根据显示器名字获取排序位置
        std::optional<int> MonitorIndexByName(const std::string& name) const override;

        void HandleAppEvent(const std::shared_ptr<AppBaseEvent>& event) override;

        VirtualDesktopBoundRectangleInfo VirtualDesktopBounds() const override;

        // Capturing target <==> information
        std::map<std::string, WorkingCaptureInfoPtr> WorkingCaptures() const override;

        using MediaBacklogProbe = std::function<std::int64_t()>;
        void ConfigureMediaBacklogProbe(MediaBacklogProbe probe);
        [[nodiscard]] std::int64_t GetNetworkMediaBacklog() const;

    private:
        bool InitializeVideoCaptures();
        void InitializeCursorCapture();
        std::vector<SupportedResolution> GetSupportedResolutions(const std::wstring& name);
        void CalculateVirtualDeskInfo();
        void NotifyCaptureMonitorInfo();

        void RestartCapturing();

        void HandleDisplayDeviceChange() override;

        bool ExistCaptureMonitor(const std::string& name);

    private:
        std::map<std::string, CaptureMonitorInfo> monitors_;
        mutable px::ConcurrentHashMap<std::string, std::shared_ptr<DesktopCaptureSource>> captures_;
        std::recursive_mutex capture_control_mutex_;
        std::vector<CaptureMonitorInfo> sorted_monitors_;
        std::shared_ptr<CursorCapture> cursor_capture_ = nullptr;
        std::shared_ptr<Thread> cursor_capture_thread_ = nullptr;

        VirtualDesktopBoundRectangleInfo virtual_desktop_bound_rectangle_info_;
        MediaBacklogProbe media_backlog_probe_;
    };

}



#endif  // PX_DDA_CAPTURE_SOURCE_H
