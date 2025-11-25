
#include "gdi_capture_plugin.h"
#include "plugin_interface/gr_plugin_events.h"
#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "tc_common_new/image.h"
#include "tc_common_new/math_helper.h"
#include "render/plugins/plugin_ids.h"
#include "gdi_capture.h"

void* GetInstance() {
    static tc::GdiCapturePlugin plugin;
    return (void*)&plugin;
}

namespace tc
{

    GdiCapturePlugin::GdiCapturePlugin() : GrMonitorCapturePlugin() {

    }

    std::string GdiCapturePlugin::GetPluginId() {
        return kGdiCapturePluginId;
    }

    std::string GdiCapturePlugin::GetPluginName() {
        return "GDI";
    }

    std::string GdiCapturePlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t GdiCapturePlugin::GetVersionCode() {
        return 110;
    }

    std::string GdiCapturePlugin::GetPluginDescription() {
        return "GDI desktop capture";
    }

    void GdiCapturePlugin::On1Second() {
        GrPluginInterface::On1Second();
    }
    
    bool GdiCapturePlugin::OnCreate(const tc::GrPluginParam &param) {
        GrMonitorCapturePlugin::OnCreate(param);
        LOGI("GdiCapturePlugin OnCreate");
        return true;
    }

    bool GdiCapturePlugin::OnDestroy() {
        GrMonitorCapturePlugin::OnDestroy();
        for (const auto& [mon, capture] : captures_) {
            capture->PauseCapture();
            capture->StopCapture();
        }
        return true;
    }

    std::vector<CaptureMonitorInfo> GdiCapturePlugin::GetCaptureMonitorInfo() {
        if (!IsWorking()) {
            return {};
        }
        return sorted_monitors_;
    }

    void GdiCapturePlugin::SetCaptureMonitor(const std::string& name) {
        bool use_default_monitor = false;
        if (name.empty()) {
            use_default_monitor = true;
        }
        LOGI("SetCaptureMonitor: {}, use_default_monitor: {}", name, use_default_monitor);

        // todo: capture all monitors at same time
        if (IsWorking()) {
            if (kAllMonitorsNameSign == name) {
                capturing_monitor_name_ = name;
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
                            capturing_monitor_name_ = name;
                            capture->ResumeCapture();
                        }
                        else {
                            capture->PauseCapture();
                        }
                    }
                    else {
                        if (!capture->IsInitSuccess()) {
                            // 如果StartCapturing后，接着执行SetCaptureMonitor，这时候 capture->IsInitSuccess () 返回 false
                            LOGW("Capture for: {} is not valid now.", monitor_name);
                            continue;
                        }
                        if (use_default_monitor && capture->IsPrimaryMonitor()) {
                            LOGI("Use default monitor: {}", monitor_name);
                            capturing_monitor_name_ = monitor_name;
                            capture->ResumeCapture();
                        }
                        else {
                            capture->PauseCapture();
                        }
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
        //LOGI("Capturing monitor name: {}", capturing_monitor_name_);
        NotifyCaptureMonitorInfo();
    }

    std::optional<int> GdiCapturePlugin::GetMonIndexByName(const std::string& name) {
        int mon_index = 0;
        for (const auto& monitor : sorted_monitors_) {
            if (name == monitor.name_) {
                return { mon_index };
            }
            ++mon_index;
        }
        return { std::nullopt };
    }

    void GdiCapturePlugin::SetCaptureFps(int fps) {
        GrMonitorCapturePlugin::SetCaptureFps(fps);
        if (IsWorking()) {
            for (const auto& [dev_name, capture] : captures_) {
                capture->SetCaptureFps(fps);
            }
        }
    }

    void GdiCapturePlugin::OnNewClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& conn_type) {
        GrPluginInterface::OnNewClientConnected(visitor_device_id, stream_id, conn_type);
        for (const auto& [k, capture] : captures_) {
            capture->RefreshScreen();
            capture->TryWakeOs();
        }
        LOGI("OnNewClientConnected!");
        NotifyCaptureMonitorInfo();

        SetCaptureMonitor(capturing_monitor_name_);

        this->InsertIdr();
    }

    void GdiCapturePlugin::DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
        GrPluginInterface::DispatchAppEvent(event);
        //LOGI("GdiCapturePlugin DispatchAppEvent type: {}", static_cast<int>(event->type_));
        if (!event) {
            return;
        }
        switch (event->type_)
        {
        case AppBaseEvent::EType::kDisplayDeviceChange: {
            LOGI("GdiCapturePlugin DispatchAppEvent is kDisplayDeviceChange");
            HandleDisplayDeviceChangeEvent();
            break;
        }
        default:
            break;
        }
    }

    std::map<std::string, WorkingCaptureInfoPtr> GdiCapturePlugin::GetWorkingCapturesInfo() {
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

    std::string GdiCapturePlugin::GetCapturingMonitorName() {
        return capturing_monitor_name_;
    }
    
    bool GdiCapturePlugin::StartCapturing() {
        GrMonitorCapturePlugin::StartCapturing();
        StopCapturing();

        CreateCaptures();
        if (monitors_.empty()) {
            LOGE("Can't find any monitors.");
            return false;
        }

        if (capturing_monitor_name_ != kAllMonitorsNameSign && !capturing_monitor_name_.empty()) {
            if (!ExistCaptureMonitor(capturing_monitor_name_)) {
                capturing_monitor_name_ = "";
            }
        }

        for(const auto&[dev_name, monitor_info] : monitors_) {
            auto capture = std::make_shared<GdiCapture>(this, monitor_info);
            LOGI("GDIPlugin capture_fps_: {}", capture_fps_);
            capture->SetCaptureFps(capture_fps_);
            capture->StartCapture();
            captures_.insert({dev_name, capture});
        }

        NotifyCaptureMonitorInfo();

        SetCaptureMonitor(capturing_monitor_name_);
        return true;
    }

    bool GdiCapturePlugin::ExistCaptureMonitor(const std::string& name) {
        for (const auto& [dev_name, monitor_info] : monitors_) {
            if (dev_name == name) {
                return true;
            }
        }
        return false;
    }

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
        auto plugin = (GdiCapturePlugin*)dwData;
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
        info.supported_res_ = plugin->GetSupportedResolutions(StringUtil::ToWString(mon_name));
        plugin->monitors_.insert({mon_name, info});

        LOGI("Found device: {}, left: {}, top: {}, screen_width: {}, screen_height: {}",
             mon_name, info.left_, info.top_, screen_width, screen_height);
        return TRUE;
    }

    void GdiCapturePlugin::CreateCaptures() {
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

    void GdiCapturePlugin::HandleDisplayDeviceChangeEvent() {
        RestartCapturing();
    }

    void GdiCapturePlugin::RestartCapturing() {
        if (!IsPluginEnabled()) {
            return;
        }
        LOGI("GdiCapturePlugin RestartCapturing");
        StopCapturing();
        CreateCaptures();
        StartCapturing();
    }

    void GdiCapturePlugin::StopCapturing() {
        for(const auto&[dev_name, capture] : captures_) {
            capture->StopCapture();
        }
        captures_.clear();
        monitors_.clear();
    }

    void GdiCapturePlugin::NotifyCaptureMonitorInfo() {
        auto event = std::make_shared<GrPluginCapturingMonitorInfoEvent>();
        this->CallbackEvent(event);
    }

    std::vector<SupportedResolution> GdiCapturePlugin::GetSupportedResolutions(const std::wstring& name) {
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

    void GdiCapturePlugin::CalculateVirtualDeskInfo() {
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

    VirtualDesktopBoundRectangleInfo GdiCapturePlugin::GetVirtualDesktopBoundRectangleInfo() {
        return virtual_desktop_bound_rectangle_info_;
    }

}
