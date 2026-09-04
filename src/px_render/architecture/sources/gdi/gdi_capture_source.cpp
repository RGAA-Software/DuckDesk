
#include "gdi_capture_source.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_common_new/log.h"
#include "px_common_new/file.h"
#include "px_common_new/image.h"
#include "px_common_new/math_helper.h"
#include "px_render/modules/module_ids.h"
#include "gdi_capture.h"
#include <chrono>
#include <thread>


namespace px
{

    GdiCaptureSource::GdiCaptureSource() : MonitorCaptureSource() {

    }

    std::string GdiCaptureSource::Id() const {
        return kGdiCaptureSourceId;
    }

    std::string GdiCaptureSource::Name() const {
        return "GDI";
    }

    std::string GdiCaptureSource::VersionName() const {
        return "1.1.0";
    }

    uint32_t GdiCaptureSource::VersionCode() const {
        return 110;
    }

    std::string GdiCaptureSource::Description() const {
        return "GDI desktop capture";
    }

    void GdiCaptureSource::Tick1Second() {
        RenderModule::Tick1Second();
    }
    
    bool GdiCaptureSource::Start(const px::RenderModuleConfiguration &param) {
        MonitorCaptureSource::Start(param);
        LOGI("GdiCaptureSource Start");
        return true;
    }

    bool GdiCaptureSource::Destroy() {
        MonitorCaptureSource::Stop();
        for (const auto& [mon, capture] : captures_) {
            capture->PauseCapture();
            capture->StopCapture();
        }
        captures_.clear();
        return MonitorCaptureSource::Destroy();
    }

    std::vector<CaptureMonitorInfo> GdiCaptureSource::CaptureMonitors() const {
        if (!IsWorking()) {
            return {};
        }
        return sorted_monitors_;
    }

    void GdiCaptureSource::SelectMonitor(const std::string& name) {
        std::scoped_lock control_lock(capture_control_mutex_);
        bool use_default_monitor = false;
        if (name.empty()) {
            use_default_monitor = true;
        }
        LOGI("SelectMonitor: {}, use_default_monitor: {}, working: {}", name, use_default_monitor, IsWorking());

        if (!IsWorking()) {
            return;
        }

        if (kAllMonitorsNameSign == name) {
            selected_monitor_name_ = name;
            // TODO
            for (const auto& [monitor_name, capture]: captures_) {
                if (!capture->IsInitSuccess()) {
                    LOGW("Capture for: {} is not valid now.", monitor_name);
                    continue;
                }
                capture->ResumeCapture();
            }
        }
        else {
            for (const auto &[monitor_name, capture]: captures_) {
                if (!name.empty()) {
                    if (monitor_name == name) {
                        selected_monitor_name_ = name;
                        capture->ResumeCapture();
                    }
                    else {
                        capture->PauseCapture();
                    }
                }
                else {
                    if (!capture->IsInitSuccess()) {
                        // 如果StartCapturing后，接着执行SelectMonitor，这时候 capture->IsInitializeSuccess () 返回 false
                        LOGW("Capture for: {} is not valid now.", monitor_name);
                        continue;
                    }
                    if (use_default_monitor && capture->IsPrimaryMonitor()) {
                        LOGI("Resume the capture for: {}, this is the default monitor", monitor_name);
                        selected_monitor_name_ = monitor_name;
                        capture->ResumeCapture();
                    }
                    else {
                        LOGI("Pause the capture for: {}", monitor_name);
                        capture->PauseCapture();
                    }
                }
            }
        }

        bool has_resumed_capture = false;
        for (const auto &[monitor_name, capture]: captures_) {
            if (!capture->IsPausing()) {
                has_resumed_capture = true;
            }
        }
        if (!has_resumed_capture) {
            LOGW("Don't has resumed capture for: {}", name);
        }
        //LOGI("Capturing monitor name: {}", selected_monitor_name_);
        NotifyCaptureMonitorInfo();
    }

    std::optional<int> GdiCaptureSource::MonitorIndexByName(const std::string& name) const {
        int mon_index = 0;
        for (const auto& monitor : sorted_monitors_) {
            if (name == monitor.name_) {
                return { mon_index };
            }
            ++mon_index;
        }
        return { std::nullopt };
    }

    void GdiCaptureSource::SetCaptureFps(int fps) {
        MonitorCaptureSource::SetCaptureFps(fps);
        if (IsWorking()) {
            for (const auto& [dev_name, capture] : captures_) {
                capture->SetCaptureFps(fps);
            }
        }
    }

    void GdiCaptureSource::OnClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& conn_type) {
        RenderModule::OnClientConnected(visitor_device_id, stream_id, conn_type);
        for (const auto& [k, capture] : captures_) {
            capture->RefreshScreen();
            capture->TryWakeOs();
        }
        // 注意:此事件会广播给所有采集插件,不代表本插件是当前激活的采集器
        LOGI("OnClientConnected! (broadcast event, working: {})", IsWorking());
        NotifyCaptureMonitorInfo();

        SelectMonitor(selected_monitor_name_);

        this->RequestKeyFrame();
    }

    void GdiCaptureSource::HandleAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
        RenderModule::HandleAppEvent(event);
        //LOGI("GdiCaptureSource HandleAppEvent type: {}", static_cast<int>(event->type_));
        if (!event) {
            return;
        }
        switch (event->type_)
        {
        case AppBaseEvent::EType::kDisplayDeviceChange: {
            LOGI("GdiCaptureSource HandleAppEvent is kDisplayDeviceChange");
            HandleDisplayDeviceChange();
            break;
        }
        default:
            break;
        }
    }

    std::map<std::string, WorkingCaptureInfoPtr> GdiCaptureSource::WorkingCaptures() const {
        std::map<std::string, WorkingCaptureInfoPtr> result;
        for (const auto& [name, capture] : captures_) {
            if (capture->IsPausing()) {
                continue;
            }
            const auto& my_monitor = capture->GetMyMonitorInfo();
            result.insert({name, std::make_shared<WorkingCaptureInfo>(WorkingCaptureInfo {
                .target_name_ = name,
                .fps_ = capture->GetCapturingFps(),
                .capture_type_ = kCaptureTypeDXGI,
                .capture_frame_width_ = my_monitor.Width(),
                .capture_frame_height_ = my_monitor.Height(),
                .capture_gaps_ = capture->GetCaptureGaps(),
            })});
        }
        return result;
    }

    std::string GdiCaptureSource::CapturingMonitorName() const {
        return selected_monitor_name_;
    }

    bool GdiCaptureSource::InitializeCapture() {
        return true;
    }

    bool GdiCaptureSource::StartCapturing() {
        std::scoped_lock control_lock(capture_control_mutex_);
        StopCapturing();

        CreateCaptures();
        if (monitors_.empty()) {
            LOGE("Can't find any monitors.");
            return false;
        }

        if (selected_monitor_name_ != kAllMonitorsNameSign && !selected_monitor_name_.empty()) {
            if (!ExistCaptureMonitor(selected_monitor_name_)) {
                selected_monitor_name_ = "";
            }
        }

        const auto owner = std::dynamic_pointer_cast<GdiCaptureSource>(
            shared_from_this());
        for(const auto&[dev_name, monitor_info] : monitors_) {
            auto capture = std::make_shared<GdiCapture>(owner, monitor_info);
            if (!capture->Init()) {
                LOGE("GDI capture init failed! {}", dev_name);
                return false;
            }
            LOGI("GDI capture_fps_: {}", capture_fps_);
            capture->SetCaptureFps(capture_fps_);
            capture->StartCapture();
            captures_.insert({dev_name, capture});

            SelectMonitor(selected_monitor_name_);
        }

        NotifyCaptureMonitorInfo();
        return true;
    }

    bool GdiCaptureSource::ExistCaptureMonitor(const std::string& name) {
        for (const auto& [dev_name, monitor_info] : monitors_) {
            if (dev_name == name) {
                return true;
            }
        }
        return false;
    }

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
        auto& source = *reinterpret_cast<GdiCaptureSource*>(dwData); // NOLINT(gammaray-raw-pointer-boundary): synchronous Win32 enumeration callback
        MONITORINFOEX monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFOEX);
        GetMonitorInfo(hMonitor, &monitorInfo);

        auto mon_name = StringUtil::ToUTF8(monitorInfo.szDevice);
        int screen_width = lprcMonitor->right - lprcMonitor->left;
        int screen_height = lprcMonitor->bottom - lprcMonitor->top;

        bool primary = false;
        if (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) {
            primary = true;
            LOGI("Found primary monitor: {}", mon_name);
        }

        CaptureMonitorInfo info;
        info.primary_ = primary;
        info.name_ = mon_name;
        info.attached_desktop_ = true;
        info.left_ = lprcMonitor->left;
        info.top_ = lprcMonitor->top;
        info.right_ = lprcMonitor->right;
        info.bottom_ = lprcMonitor->bottom;
        info.supported_res_ = source.GetSupportedResolutions(StringUtil::ToWString(mon_name));
        source.monitors_.insert({mon_name, info});

        LOGI("Found device: {}, left: {}, top: {}, screen_width: {}, screen_height: {}",
             mon_name, info.left_, info.top_, screen_width, screen_height);
        return TRUE;
    }

    void GdiCaptureSource::CreateCaptures() {
        EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)this);

        //CaptureMonitorInfo cap_mon_info;
        //cap_mon_info.virtual_desktop_left_ = GetSystemMetrics(SM_XVIRTUALSCREEN);
        //cap_mon_info.virtual_desktop_top_ = GetSystemMetrics(SM_YVIRTUALSCREEN);
        //cap_mon_info.virtual_desktop_width_ = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        //cap_mon_info.virtual_desktop_height_ = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        //cap_mon_info.virtual_desktop_width_ = MathHelper::AlignTo4Bytes(cap_mon_info.virtual_desktop_width_); // 直接4字节对齐, 方便后面直接进行内存copy,不然还得进行 '行' 内存拷贝
        //LOGI("cap_mon_info.virtual_desktop_left_: {}, cap_mon_info.virtual_desktop_top_: {}, ", cap_mon_info.virtual_desktop_left_, cap_mon_info.virtual_desktop_top_);
        //LOGI("cap_mon_info.virtual_desktop_width_: {}, cap_mon_info.virtual_desktop_height_: {}, ", cap_mon_info.virtual_desktop_width_, cap_mon_info.virtual_desktop_height_);
        //cap_mon_info.name_ = kVirtualDesktopNameSign;
        //gdi_capture_ = GdiCapture::Make(this, cap_mon_info);

        CalculateVirtualDeskInfo();
    }

    void GdiCaptureSource::HandleDisplayDeviceChange() {
        RestartCapturing();
    }

    void GdiCaptureSource::RestartCapturing() {
        std::scoped_lock control_lock(capture_control_mutex_);
        if (!IsEnabled()) {
            return;
        }
        const auto old_count = monitors_.size();
        LOGI("GdiCaptureSource RestartCapturing, old monitor count: {}", old_count);
        constexpr int kMaxAttempts = 3;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            if (StartCapturing()) {
                LOGI("GDI topology rebuild succeeded on attempt {}, monitors: {} -> {}",
                     attempt, old_count, monitors_.size());
                return;
            }
            LOGW("GDI topology rebuild attempt {}/{} failed", attempt, kMaxAttempts);
            std::this_thread::sleep_for(std::chrono::milliseconds(150 * attempt));
        }
        LOGE("GDI topology rebuild failed after {} attempts", kMaxAttempts);
    }

    void GdiCaptureSource::StopCapturing() {
        std::scoped_lock control_lock(capture_control_mutex_);
        for(const auto&[dev_name, capture] : captures_) {
            capture->StopCapture();
        }
        captures_.clear();
        monitors_.clear();
    }

    void GdiCaptureSource::NotifyCaptureMonitorInfo() {
        if (sorted_monitors_.empty()) {
            LOGI("==> Sorted Monitor's empty, ignore the PxPluginCapturingMonitorInfoEvent");
            return;
        }
        const auto event = std::make_shared<PxPluginCapturingMonitorInfoEvent>();
        this->EmitCompatibilityEvent(event);
    }

    std::vector<SupportedResolution> GdiCaptureSource::GetSupportedResolutions(const std::wstring& name) {
        std::vector<SupportedResolution> resolutions;
        DEVMODE dm;
        dm.dmSize = sizeof(dm);
        dm.dmDriverExtra = 0;
        int mode_num = 0;
        while (EnumDisplaySettingsExW(name.c_str(), mode_num, &dm, 0)) {
            mode_num++;
            bool found = false;
            for (auto& res : resolutions) {
                if (res.width_ == dm.dmPelsWidth && res.height_ == dm.dmPelsHeight) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                resolutions.push_back(SupportedResolution{
                    .width_ = dm.dmPelsWidth,
                    .height_ = dm.dmPelsHeight,
                });
            }
        }
        return resolutions;
    }

    void GdiCaptureSource::CalculateVirtualDeskInfo() {
        sorted_monitors_.clear();
        int total_width = 0;
        int max_height = 0;
        int max_virtual_coord = 65535;
        for (auto& [idx, info] : monitors_) {
            sorted_monitors_.push_back(info);
            total_width += info.Width();
            if (info.Height() > max_height) {
                max_height = info.Height();
            }
            LOGI("ORIGIN, idx: {}, left: {}", idx, info.left_);
        }

        std::sort(sorted_monitors_.begin(), sorted_monitors_.end(), [](const CaptureMonitorInfo& lh, const CaptureMonitorInfo& rh) -> bool {
            return lh.left_ < rh.left_;
        });

        // to do 未测试0显示器的时候
        if (sorted_monitors_.empty()) {
            return;
        }

        int far_left = sorted_monitors_[0].left_, far_top = sorted_monitors_[0].top_, far_right = sorted_monitors_[0].right_, far_bottom = sorted_monitors_[0].bottom_;

        for (auto& info : sorted_monitors_) {

            if (info.left_ < far_left) {
                far_left = info.left_;
            }

            if (info.top_ < far_top) {
                far_top = info.top_;
            }

            if (info.right_ > far_right) {
                far_right = info.right_;
            }

            if (info.bottom_ > far_bottom) {
                far_bottom = info.bottom_;
            }
        }
        virtual_desktop_bound_rectangle_info_.far_left_ = far_left;
        virtual_desktop_bound_rectangle_info_.far_top_ = far_top;
        virtual_desktop_bound_rectangle_info_.far_right_ = far_right;
        virtual_desktop_bound_rectangle_info_.far_bottom_ = far_bottom;
        LOGI("{}", virtual_desktop_bound_rectangle_info_.Dump());
    }

    VirtualDesktopBoundRectangleInfo GdiCaptureSource::VirtualDesktopBounds() const {
        return virtual_desktop_bound_rectangle_info_;
    }

}
