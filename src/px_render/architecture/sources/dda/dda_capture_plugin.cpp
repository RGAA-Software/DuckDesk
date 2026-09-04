//
// Created RGAA on 15/11/2024.
//

#include <ShlObj_core.h>

#include <memory>
#include "dda_capture_plugin.h"

#include <ranges>
#include <chrono>
#include <thread>

#include "px_render/modules/module_ids.h"
#include "dda_capture.h"
#include "cursor_capture.h"
#include "px_common_new/log.h"
#include "px_common_new/thread.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugin_interface/px_plugin_context.h"


namespace px
{

    void DDACapturePlugin::ConfigureMediaBacklogProbe(
        MediaBacklogProbe probe) {
        media_backlog_probe_ = std::move(probe);
    }

    std::int64_t DDACapturePlugin::GetNetworkMediaBacklog() const {
        return media_backlog_probe_ ? media_backlog_probe_() : 0;
    }

    DDACapturePlugin::DDACapturePlugin() : PxMonitorCapturePlugin() {

    }

    std::string DDACapturePlugin::GetPluginId() {
        return kDdaCapturePluginId;
    }

    std::string DDACapturePlugin::GetPluginName() {
        return "DXGI";
    }

    std::string DDACapturePlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t DDACapturePlugin::GetVersionCode() {
        return 110;
    }

    std::string DDACapturePlugin::GetPluginDescription() {
        return "DXGI desktop duplication";
    }

    bool DDACapturePlugin::OnCreate(const px::PxPluginParam& param) {
        PxMonitorCapturePlugin::OnCreate(param);
        InitCursorCapture();
        return true;
    }

    bool DDACapturePlugin::InitVideoCaptures() {
        HRESULT res = 0;
        int adapter_index = 0;
        CComPtr<IDXGIFactory1> factory1_ = nullptr;
        res = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **) &factory1_);
        if (res != S_OK) {
            LOGE("!!! CreateDXGIFactory1 failed, this plugin can't work !!!");
            return false;
        }
        
        do {
            CComPtr<IDXGIAdapter1> adapter1 = nullptr;
            CComPtr<ID3D11Device> d3d11_device = nullptr;
            CComPtr<ID3D11DeviceContext> d3d11_device_context = nullptr;
            LOGI("Will query Adapter: {}", adapter_index);
            res = factory1_->EnumAdapters1(adapter_index, &adapter1);
            if (res != S_OK) {
                // 枚举到尾部时返回 DXGI_ERROR_NOT_FOUND,属正常结束
                if (res == DXGI_ERROR_NOT_FOUND) {
                    LOGI("EnumAdapters1 finished, total adapters: {}", adapter_index);
                }
                else {
                    LOGE("EnumAdapters1 failed, index: {}, res: 0x{:x}", adapter_index, res);
                }
                break;
            }
            D3D_FEATURE_LEVEL feature_level;
            DXGI_ADAPTER_DESC adapter_desc{};
            adapter1->GetDesc(&adapter_desc);
            auto adapter_uid = adapter_desc.AdapterLuid.LowPart;
            LOGI("Adapter Index:{} Name:{}, Uid:{}", adapter_index, StringUtil::ToUTF8(adapter_desc.Description).c_str(), adapter_uid);
            res = D3D11CreateDevice(adapter1, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr, 0, D3D11_SDK_VERSION, &d3d11_device, &feature_level,
                                    &d3d11_device_context);
            if (res != S_OK || !d3d11_device) {
                LOGE("D3D11CreateDevice failed: 0x{:x} {}", res, StringUtil::GetErrorStr(res).c_str());
                break;
            }
            if (feature_level < D3D_FEATURE_LEVEL_11_0) {
                LOGE("D3D11CreateDevice returns an instance without DirectX 11 support, level : {}  Following initialization may fail",(int) feature_level);
                break;
            }
            CComPtr<IDXGIDevice> dxgi_device;
            res = d3d11_device.QueryInterface(&dxgi_device);
            if (res != S_OK || !dxgi_device) {
                LOGE("ID3D11Device is not an implementation of IDXGIDevice, this usually means the system does not support DirectX 11. Error:{}, code: {}",
                     StringUtil::GetErrorStr(res), res);
                break;
            }

            int monitor_index = -1;
            do {
                ++monitor_index;
                CComPtr<IDXGIOutput> output;
                res = adapter1->EnumOutputs(monitor_index, &output);
                if (res == DXGI_ERROR_NOT_FOUND) {
                    // 枚举到尾部(或无输出的适配器,如未启用的虚拟显示器)的正常返回,
                    // 不是错误;RDP 场景才可能真的无输出,仅在 0 号输出缺失时提示。
                    if (monitor_index == 0) {
                        LOGI("Adapter {} has no outputs (virtual display or RDP session?), skip.", adapter_index);
                    }
                    break;
                }
                if (res == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
                    LOGE("IDXGIAdapter::EnumOutputs returns NOT_CURRENTLY_AVAILABLE. This may happen when running in session 0");
                    break;
                }
                if (res != S_OK || !output) {
                    LOGE("IDXGIAdapter::EnumOutputs returns an unexpected result {} with error code {}", StringUtil::GetErrorStr(res).c_str(), res);
                    break;
                }
                if (res == S_OK) {
                    LOGI("EnumOutputs ok Adapter Index:{} Name:{}, Uid:{}", adapter_index, StringUtil::ToUTF8(adapter_desc.Description).c_str(), adapter_uid);
                }
                
                DXGI_OUTPUT_DESC output_desc{};
                res = output->GetDesc(&output_desc);
                if (res == S_OK) {
                    if (output_desc.AttachedToDesktop && IsValidRect(output_desc.DesktopCoordinates))
                    {
                        auto dev_name = StringUtil::ToUTF8(output_desc.DeviceName);
                        LOGI("EnumOutputs S_OK, name_: {}", dev_name);
                        LOGI("Adapter Index:{} Name:{}, Uid:{}", adapter_index, StringUtil::ToUTF8(adapter_desc.Description).c_str(), adapter_uid);
                        monitors_.insert(
                            { 
                                dev_name, CaptureMonitorInfo {
                                    .name_ = dev_name,
                                    .attached_desktop_ = (bool)output_desc.AttachedToDesktop,
                                    .top_ = output_desc.DesktopCoordinates.top,
                                    .left_ = output_desc.DesktopCoordinates.left,
                                    .right_ = output_desc.DesktopCoordinates.right,
                                    .bottom_ = output_desc.DesktopCoordinates.bottom,
                                    .supported_res_ = GetSupportedResolutions(output_desc.DeviceName),
                                    .adapter_uid_ = adapter_uid,
                                } 
                            }
                        );
                    }
                    else {
                        LOGI("Adapter index: {}, name: {}; dev_name: {}, AttachedToDesktop: {} is ignored.",adapter_index, StringUtil::ToUTF8(adapter_desc.Description), StringUtil::ToUTF8(output_desc.DeviceName),
                            (bool)output_desc.AttachedToDesktop);
                    }
                } else {
                    LOGE("Failed to get output description of device: {} in adapter index: {}, adaper id: {}",
                         monitor_index, adapter_index, adapter_uid);
                    break;
                }
            } while(true);
            
            adapter_index++;
            
        } while(true);

        CalculateVirtualDeskInfo();

        LOGI("Finally, we got monitor size: {}", monitors_.size());
        for(const auto&[dev_name, monitor_info] : monitors_) {
            LOGI("In adapter:{}, the monitor:[{}]=>{}", monitor_info.adapter_uid_, dev_name, monitors_[dev_name].Dump());
        }
        if (monitors_.empty()) {
            LOGE("!! Don't have monitors!");
        }
        return !monitors_.empty();
    }

    void DDACapturePlugin::InitCursorCapture() {
        cursor_capture_thread_ = Thread::MakeOnceTask([=, this]() {
            cursor_capture_ = std::make_shared<CursorCapture>(this);
            while (!destroyed_) {
                cursor_capture_->Capture();
                auto target_duration = 1000 / capture_fps_;
                std::this_thread::sleep_for(std::chrono::milliseconds(target_duration));
            }
        }, "", false);
    }

    bool DDACapturePlugin::OnDestroy() {
        PxMonitorCapturePlugin::OnStop();
        StopCapturing();
        destroyed_ = true;
        if (cursor_capture_thread_ && cursor_capture_thread_->IsJoinable()) {
            cursor_capture_thread_->Join();
            cursor_capture_thread_ = nullptr;
        }
        cursor_capture_ = nullptr;
        return PxMonitorCapturePlugin::OnDestroy();
    }

    bool DDACapturePlugin::IsWorking() {
        return !captures_.Empty();
    }

    bool DDACapturePlugin::ExistCaptureMonitor(const std::string& name) {
        for (const auto &dev_name: monitors_ | std::views::keys) {
            if (dev_name == name) {
                return true;
            }
        }
        return false;
    }

    bool DDACapturePlugin::TryInitSpecificCapture() {
        std::scoped_lock control_lock(capture_control_mutex_);
        if (!captures_.Empty()) {
            StopCapturing();
        }
        monitors_.clear();

        auto res_init = InitVideoCaptures();
        if (!res_init) {
            LOGE("TryInitSpecificCapture, InitVideoCaptures failed!");
            return false;
        }

        for(const auto&[dev_name, monitor_info] : monitors_) {
            auto capture = std::make_shared<DDACapture>(this, monitor_info);
            LOGI("DDACapturePlugin capture_fps_: {}", capture_fps_);
            capture->SetCaptureFps(capture_fps_);
            auto init_res = capture->Init();
            if (!init_res) {
                LOGW("Will stop & clear captures.");
                auto captures = captures_.Clone();
                for (const auto& [k, v] : captures) {
                    if (v) {
                        v->StopCapture();
                    }
                }
                captures_.Clear();
                LOGE("TryInitSpecificCapture, Init DDA capture [ {} ]failed, can't start DDA capture.", dev_name);
                return false;
            }
            captures_.Insert(dev_name, capture);
        }
        return !captures_.Empty();
    }

    bool DDACapturePlugin::StartCapturing() {
        std::scoped_lock control_lock(capture_control_mutex_);
        StopCapturing();

        auto res_init = InitVideoCaptures();
        if (!res_init) {
            LOGE("InitVideoCaptures failed!");
            return false;
        }

        if (capturing_monitor_name_ != kAllMonitorsNameSign && !capturing_monitor_name_.empty()) {
            if (!ExistCaptureMonitor(capturing_monitor_name_)) {
                capturing_monitor_name_ = "";
            }
        }

        for(const auto&[dev_name, monitor_info] : monitors_) {
            auto capture = std::make_shared<DDACapture>(this, monitor_info);
            LOGI("DDACapturePlugin capture_fps_: {}", capture_fps_);
            capture->SetCaptureFps(capture_fps_);
            auto init_res = capture->Init();
            if (!init_res) {
                LOGE("Init DDA capture [ {} ]failed, can't start DDA capture.", dev_name);
                //captures_.Clear();
                //return false;
                continue;
            }

            // set error callback
            capture->SetDDAErrorCallback([=, this](const MonitorCaptureError& err) {
                if (capture_err_callback_) {
                    capture_err_callback_(err);
                }
            });

            // init success
            if (kAllMonitorsNameSign == capturing_monitor_name_) {
                capture->ResumeCapture();
            }
            else if(capturing_monitor_name_.empty()) {
                if (capture->IsPrimaryMonitor()) {
                    capture->ResumeCapture();
                }
                capturing_monitor_name_ = capture->GetMyMonitorInfo().name_;
            }
            else if (capture->GetMyMonitorInfo().name_ == capturing_monitor_name_) {
                capture->ResumeCapture();
            }
            SetCaptureMonitor(capturing_monitor_name_);

            // start capturing
            capture->StartCapture();

            captures_.Insert(dev_name, capture);
        }

        NotifyCaptureMonitorInfo();

        return !captures_.Empty();
    }

    void DDACapturePlugin::StopCapturing() {
        std::scoped_lock control_lock(capture_control_mutex_);
        auto captures = captures_.Clone();
        for (const auto& [k, v] : captures) {
            if (v) {
                v->StopCapture();
            }
        }
        captures_.Clear();
        monitors_.clear();
        sorted_monitors_.clear();
    }

    void DDACapturePlugin::RestartCapturing() {
        std::scoped_lock control_lock(capture_control_mutex_);
        const auto old_count = monitors_.size();
        LOGI("DDACapturePlugin RestartCapturing, old monitor count: {}", old_count);
        constexpr int kMaxAttempts = 3;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            if (StartCapturing()) {
                LOGI("DDA topology rebuild succeeded on attempt {}, monitors: {} -> {}",
                     attempt, old_count, monitors_.size());
                return;
            }
            LOGW("DDA topology rebuild attempt {}/{} failed", attempt, kMaxAttempts);
            std::this_thread::sleep_for(std::chrono::milliseconds(150 * attempt));
        }
        LOGE("DDA topology rebuild failed after {} attempts", kMaxAttempts);
        // A display add/remove can invalidate every Desktop Duplication
        // object.  If recreating them also fails (for example because the
        // graphics device cannot currently be allocated), notify the
        // application so it can downgrade to GDI.  Without this callback the
        // active plugin remains DDA with an empty capture/monitor set and
        // connected Web clients stay black indefinitely.
        if (capture_err_callback_) {
            capture_err_callback_(MonitorCaptureError::kCantCapture);
        }
    }

    std::vector<CaptureMonitorInfo> DDACapturePlugin::GetCaptureMonitorInfo() {
        if (!IsWorking()) {
            return {};
        }
        return sorted_monitors_;
    }

    std::string DDACapturePlugin::GetCapturingMonitorName() {
        return capturing_monitor_name_;
    }

    void DDACapturePlugin::SetCaptureMonitor(const std::string& name) {
        std::scoped_lock control_lock(capture_control_mutex_);
        bool use_default_monitor = false;
        if (name.empty()) {
            use_default_monitor = true;
        }
        // 注意:客户端连接事件会广播给所有采集插件,此日志不代表本插件是激活采集器
        LOGI("SetCaptureMonitor: {}, use_default_monitor: {}, working: {}", name, use_default_monitor, IsWorking());

        // todo: capture all monitors at same time
        if (IsWorking()) {
            if (kAllMonitorsNameSign == name) {
                capturing_monitor_name_ = name;
                // TODO
                auto captures = captures_.Clone();
                for (const auto& [k, capture] : captures) {
                    if (capture) {
                        if (!capture->IsInitSuccess()) {
                            LOGW("Capture for: {} is not valid now.", k);
                            continue;
                        }
                        capture->ResumeCapture();
                    }
                }
            }
            else {
                auto captures = captures_.Clone();
                for (const auto& [monitor_name, capture] : captures) {
                    if (!capture) {
                        continue;
                    }
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
                            LOGW("Capture for: {} is not valid now.", monitor_name);  // 如果StartCapturing后，接着执行SetCaptureMonitor，这时候 capture->IsInitSuccess () 返回 false
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
        captures_.ApplyAll([&](const auto& k, const std::shared_ptr<DesktopCaptureSource>& capture) {
            if (capture && !capture->IsPausing()) {
                has_resumed_capture = true;
            }
        });
        if (!has_resumed_capture) {
            LOGW("Don't has resumed capture for: {}", name);
        }
        //LOGI("Capturing monitor name: {}", capturing_monitor_name_);
        NotifyCaptureMonitorInfo();
    }

    void DDACapturePlugin::SetCaptureFps(int fps) {
        PxMonitorCapturePlugin::SetCaptureFps(fps);
        if (IsWorking()) {
            captures_.ApplyAll([fps](const auto& k, const auto& capture) {
                if (capture) {
                    capture->SetCaptureFps(fps);
                }
            });
        }
    }

    void DDACapturePlugin::On1Second() {
        if (captures_.Empty()) {
            return;
        }

        // DXGI_ERROR_WAIT_TIMEOUT means that the desktop had no new image or
        // pointer update during the requested interval. It is a normal result
        // for a static desktop, not evidence that Desktop Duplication is
        // broken. Treating a run of timeouts as a capture failure caused a
        // needless DDA -> GDI -> DDA loop every ~20 seconds and made topology
        // reconnects race with capture-backend switching. Real duplication
        // failures are handled in DDACapture::Capture by their HRESULTs.

    }

    void DDACapturePlugin::On16MilliSecond() {
        captures_.ApplyAll([](const auto& k, const std::shared_ptr<DesktopCaptureSource>& capture) {
            if (capture) {
                capture->On16MilliSecond();
            }
        });
    }

    void DDACapturePlugin::On33MilliSecond() {
        captures_.ApplyAll([](const std::string &k, const std::shared_ptr<DesktopCaptureSource> &capture) {
            if (capture) {
                capture->On33MilliSecond();
            }
        });
    }

    void DDACapturePlugin::OnNewClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& conn_type) {
        PxPluginInterface::OnNewClientConnected(visitor_device_id, stream_id, conn_type);
        auto captures = captures_.Clone();
        for (const auto& [monitor_name, capture] : captures) {
            if (!capture) {
                continue;
            }
            capture->RefreshScreen();
            capture->TryWakeOs();
        }
        LOGI("OnNewClientConnected!");
        NotifyCaptureMonitorInfo();

        SetCaptureMonitor(capturing_monitor_name_);

        this->InsertIdr();

        // send cached texture if you have
        for (const auto& capture: captures | std::views::values) {
            if (capture) {
                capture->SendCachedTexture();
            }
        }
    }

    std::vector<SupportedResolution> DDACapturePlugin::GetSupportedResolutions(const std::wstring& name) {
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

    void DDACapturePlugin::CalculateVirtualDeskInfo() {
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
        if (sorted_monitors_.size() <= 0) {
            return;
        }

        int far_left = sorted_monitors_[0].left_, far_top = sorted_monitors_[0].top_, far_right = sorted_monitors_[0].right_, far_bottom = sorted_monitors_[0].bottom_;

        int left_monitor_virtual_size = 0;
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

    void DDACapturePlugin::NotifyCaptureMonitorInfo() {
        if (sorted_monitors_.empty()) {
            LOGI("==> Sorted Monitor's empty, ignore the PxPluginCapturingMonitorInfoEvent");
            return;
        }
        const auto event = std::make_shared<PxPluginCapturingMonitorInfoEvent>();
        this->CallbackEvent(event);
    }

    std::optional<int> DDACapturePlugin::GetMonIndexByName(const std::string& name) {
        int mon_index = 0;
        for (const auto& monitor : sorted_monitors_) {
            if (name == monitor.name_) {
                return { mon_index };
            }
            ++mon_index;
        }
        return { std::nullopt };
    }

    void DDACapturePlugin::DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
        PxPluginInterface::DispatchAppEvent(event);
        //LOGI("DDACapturePlugin DispatchAppEvent type: {}", static_cast<int>(event->type_));
        if (!event) {
            return;
        }
        switch (event->type_)
        {
        case AppBaseEvent::EType::kDisplayDeviceChange: {
            LOGI("DDACapturePlugin DispatchAppEvent is kDisplayDeviceChange");
            HandleDisplayDeviceChangeEvent();
            break;
        }
        default:
            break;
        }
    }

    void DDACapturePlugin::HandleDisplayDeviceChangeEvent() {
        RestartCapturing();
    }

    VirtualDesktopBoundRectangleInfo DDACapturePlugin::GetVirtualDesktopBoundRectangleInfo() {
        return virtual_desktop_bound_rectangle_info_;
    }

    std::map<std::string, WorkingCaptureInfoPtr> DDACapturePlugin::GetWorkingCapturesInfo() {
        std::map<std::string, WorkingCaptureInfoPtr> result;
        auto captures = captures_.Clone();
        for (const auto& [name, capture] : captures) {
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
}
