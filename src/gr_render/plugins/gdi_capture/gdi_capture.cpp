#include "gdi_capture.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <timeapi.h>
#include "tc_common_new/string_util.h"
#include "tc_common_new/message_notifier.h"
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "tc_common_new/time_util.h"
#include "tc_common_new/monitors.h"
#include "tc_common_new/image.h"
#include "tc_common_new/math_helper.h"
#include "tc_common_new/win32/win_helper.h"
#include "tc_capture_new/capture_message.h"
#include "plugin_interface/gr_plugin_events.h"
#include "gdi_capture_plugin.h"
#include "tc_common_new/win32/d3d_debug_helper.h"


namespace tc
{

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
        auto gdi_capture = (GdiCapture*)dwData;
        MONITORINFOEX monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFOEX);
        GetMonitorInfoW(hMonitor, &monitorInfo);

        auto it_mon_name = std::wstring(monitorInfo.szDevice);
        if (it_mon_name != gdi_capture->mon_name_) {
            return TRUE;
        }

        int screen_width = lprcMonitor->right - lprcMonitor->left;
        int screen_height = lprcMonitor->bottom - lprcMonitor->top;

        gdi_capture->left_ = lprcMonitor->left;
        gdi_capture->top_ = lprcMonitor->top;
        gdi_capture->right_ = lprcMonitor->right;
        gdi_capture->bottom_ = lprcMonitor->bottom;
        if ((screen_width != gdi_capture->width_ && gdi_capture->width_ > 0) || (screen_height != gdi_capture->height_ && gdi_capture->height_ > 0)) {
            gdi_capture->reinit_ = true;
            LOGW("GDI Screen size changed, origin: {}x{}, now: {}x{}", gdi_capture->width_, gdi_capture->height_, screen_width, screen_height);
        }
        gdi_capture->width_ = screen_width;
        gdi_capture->height_ = screen_height;

        //LOGI("screen_width: {}, screen_height: {}", screen_width, screen_height);
        return TRUE;
    }

    std::shared_ptr<GdiCapture> GdiCapture::Make(GdiCapturePlugin* plugin, const CaptureMonitorInfo& my_monitor_info) {
        return std::make_shared<GdiCapture>(plugin, my_monitor_info);
    }

    GdiCapture::GdiCapture(GdiCapturePlugin* plugin, const CaptureMonitorInfo& my_monitor_info)
        : PluginDesktopCapture(my_monitor_info) {
        plugin_ = plugin;
        fps_stat_ = std::make_shared<FpsStat>();
        mon_name_ = StringUtil::ToWString(my_monitor_info_.name_);
        is_primary_monitor_ = my_monitor_info_.primary_;
        LOGI("GdiCapture my monitor info: {}", my_monitor_info.Dump());
    }

    GdiCapture::~GdiCapture() {
       LOGW("GDI Released: {}", my_monitor_info_.name_);
    }

    bool GdiCapture::Init() {
        const int kInitTryMaxCount = 3;
        int try_count = -1;
        bool gdi_init_res = false;

        do {
            ++try_count;
            gdi_init_res = this->InitInternal();
            if (!gdi_init_res) {
                LOGE("gdi capture init failed for target: {}, will try again.", my_monitor_info_.name_);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            else {
                break;
            }

        } while (try_count < kInitTryMaxCount);

        LOGI("Init GDI result: {} -> {}", my_monitor_info_.name_, gdi_init_res);
        return gdi_init_res;
    }

    bool GdiCapture::InitInternal() {
        init_success_ = false;

        EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)this);
        if (width_ <= 0 || height_ <= 0) {
            LOGE("monitor size error: {}x{}", width_, height_);
            return false;
        }

        //screen_dc_ = GetDC(NULL); // 整个虚拟屏幕的设备上下文, GetDC 是采集整个虚拟屏幕的画面,GDI 作为托底采集,就采集整个虚拟桌面就可以
        screen_dc_ = CreateDCW(nullptr, mon_name_.c_str(), nullptr, nullptr); // CreateDC 可以采集特定屏幕的画面
        if (!screen_dc_) {
            LOGW("GdiCapture GetDC failed.");
            return false;
        }

        memory_dc_ = CreateCompatibleDC(screen_dc_);
        if (!memory_dc_) {
            LOGW("GdiCapture CreateCompatibleDC failed.");
            return false;
        }

        if (SetStretchBltMode(memory_dc_, COLORONCOLOR) == 0) { // 使用 COLORONCOLOR 可以提高图像缩放的速度，适合在不需要透明效果的情况下使用。
            LOGW("SetStretchBltMode failed.");
        }

        // 创建兼容位图
        bit_map_ = CreateCompatibleBitmap(screen_dc_, my_monitor_info_.Width(), my_monitor_info_.Height());
        if (!bit_map_) {
            LOGW("CreateCompatibleBitmap failed.");
            return false;
        }

        // 选择位图到内存 DC 中
        SelectObject(memory_dc_, bit_map_);
        init_success_ = true;
        LOGI("GdiCapture Init OK.");
        return true;
    }

    bool GdiCapture::IsInitSuccess() {
        return init_success_;
    }

    bool GdiCapture::Exit() {
        init_success_ = false;

        DeleteObject(bit_map_);
        bit_map_ = nullptr;

        DeleteDC(screen_dc_);
        screen_dc_ = nullptr;

        ReleaseDC(NULL, memory_dc_);
        memory_dc_ = nullptr;

        return true;
    }

    bool GdiCapture::CaptureNextFrame() {
        // 复制整个虚拟屏幕的内容到内存 DC
        BitBlt(memory_dc_, 0, 0, my_monitor_info_.Width(), my_monitor_info_.Height(), screen_dc_, 0, 0, SRCCOPY);
        BITMAP bmp;
        GetObject(bit_map_, sizeof(BITMAP), &bmp);
        
        BITMAPINFOHEADER bi;
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = bmp.bmWidth;
        bi.biHeight = -bmp.bmHeight; // 负值表示位图是自上而下的
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        bi.biSizeImage = 0;
        bi.biXPelsPerMeter = 0;
        bi.biYPelsPerMeter = 0;
        bi.biClrUsed = 0;
        bi.biClrImportant = 0;

        if (bmp.bmWidthBytes != bmp.bmWidth * 4) {
            LOGW("bmp.bmWidthBytes != bmp.bmWidth * 4. bmp.bmWidthBytes: {}, bmp.bmWidth: {}", bmp.bmWidthBytes, bmp.bmWidth);
        }

        if (bmp.bmWidthBytes <= 0 || bmp.bmHeight <= 0) {
            LOGE("invalid bitmap dimensions, width_bytes: {}, height: {}", bmp.bmWidthBytes, bmp.bmHeight);
            return false;
        }
        const auto width_bytes = static_cast<size_t>(bmp.bmWidthBytes);
        const auto bmp_height = static_cast<size_t>(bmp.bmHeight);
        if (width_bytes > (std::numeric_limits<size_t>::max)() / bmp_height) {
            LOGE("bitmap size overflow, width_bytes: {}, height: {}", bmp.bmWidthBytes, bmp.bmHeight);
            return false;
        }
        const auto dwBmpSize = width_bytes * bmp_height;

        DataPtr data_ptr = Data::Make(nullptr, dwBmpSize);
        if (!data_ptr) {
            LOGE("DataPtr Make error!");
            return false;
        }

        auto* data_addr = data_ptr->DataAddr();
        if (!data_addr) {
            LOGE("bitmap data address is null, size: {}", dwBmpSize);
            return false;
        }

        int ret = GetDIBits(memory_dc_, bit_map_, 0, (UINT)bmp.bmHeight, data_addr, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        if (ret == 0) {
            LOGW("GetDIBits failed.");
            return false;
        }

        // FFmpeg to encode
        CaptureVideoFrame cap_video_frame{};
        cap_video_frame.type_ = kCaptureVideoFrame;
        cap_video_frame.capture_type_ = kCaptureVideoByBitmapData;
        cap_video_frame.data_length = 0;
        cap_video_frame.frame_width_ = bmp.bmWidth;
        cap_video_frame.frame_height_ = bmp.bmHeight;
        cap_video_frame.frame_index_ = GetFrameIndex();
        cap_video_frame.raw_image_ = Image::Make(data_ptr, bmp.bmWidth, bmp.bmHeight, RawImageType::kBGRA);
        if (StringUtil::CopyCStringToArray(cap_video_frame.display_name_, my_monitor_info_.name_)) {
            LOGW("display_name truncated for monitor: {}, src_len: {}, dst_len: {}",
                 my_monitor_info_.name_, my_monitor_info_.name_.size(), sizeof(cap_video_frame.display_name_));
        }
        auto mon_index_res = plugin_->GetMonIndexByName(my_monitor_info_.name_);
        if (mon_index_res.has_value()) {
            cap_video_frame.monitor_index_ = mon_index_res.value();
        }
        else {
            LOGE("desktop capture get mon index by name failed!");
        }
        cap_video_frame.left_ = this->left_;
        cap_video_frame.top_ = this->top_;
        cap_video_frame.right_ = this->right_;
        cap_video_frame.bottom_ = this->bottom_;

        if (plugin_->IsPluginEnabled()) {
            auto event = std::make_shared<GrPluginCapturedVideoFrameEvent>();
            event->frame_ = cap_video_frame;
            this->plugin_->CallbackEvent(event);
        }

        // fps tick
        fps_stat_->Tick();

        // capture gaps
        auto curr_timestamp = (int64_t)TimeUtil::GetCurrentTimestamp();
        if (last_captured_timestamp_ == 0) {
            last_captured_timestamp_ = curr_timestamp;
        }
        auto diff = curr_timestamp - last_captured_timestamp_;
        if (capture_gaps_.Size() >= 180) {
            capture_gaps_.PopFront();
        }
        capture_gaps_.PushBack((int32_t)diff);
        last_captured_timestamp_ = curr_timestamp;

#if 0   // save rgb to file
        static int frame_index_ = -1;
        ++frame_index_;
        std::wstring file_name = std::wstring(L"desktop_") + std::to_wstring(frame_index_ % 12) + L".bgra";
        HANDLE hFile = CreateFileW(file_name.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD dwBytesWritten;
        WriteFile(hFile, data_ptr->DataAddr(), dwBmpSize, &dwBytesWritten, NULL);
#endif

        return true;
    }

    void GdiCapture::Start() {
        LOGI("GdiCapture::Start() stop flag : {}", stop_flag_.load());
        capture_thread_ = std::thread([this] {
            Capture();
        });
    }

    void GdiCapture::Capture() {
        LOGI("GdiCapture::Capture(), stop_flag_: {}", stop_flag_.load());
        while (!stop_flag_) {
            if (pausing_ || plugin_->DontHaveConnectedClientsNow()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(17));
                continue;
            }

            // check display size
            EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)this);

            if (!WinHelper::InputDesktopSelected() || reinit_) {
                if (!WinHelper::SelectInputDesktop()) {
                    LOGE("GDI capture SelectInputDesktop error.");
                }
                // 切换桌面后，需要重新初始化 GDI相关
                this->Exit();
                this->Init();
                plugin_->InsertIdr();

                reinit_ = false;
            }
            CaptureNextFrame();
        }
    }

    int64_t GdiCapture::GetFrameIndex() {
        monitor_frame_index_++;
        return monitor_frame_index_;
    }

    bool GdiCapture::StartCapture() {
        this->stop_flag_ = false;
        this->Start();
        return true;
    }

    bool GdiCapture::PauseCapture() {
        pausing_ = true;
        return true;
    }

    void GdiCapture::ResumeCapture() {
        pausing_ = false;
        plugin_->InsertIdr();
    }

    void GdiCapture::StopCapture() {
        stop_flag_ = true;
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        this->Exit();
    }

    void GdiCapture::RefreshScreen() {
        PluginDesktopCapture::RefreshScreen();
        used_cache_times_ = 0;
    }

    bool GdiCapture::IsPrimaryMonitor() {
        return is_primary_monitor_;
    }

    int GdiCapture::GetCapturingFps() {
        return fps_stat_->value();
    }

} // tc
