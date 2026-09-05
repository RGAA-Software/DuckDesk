#include "gdi_capture.h"
#include "px_common_new/async_runtime.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <timeapi.h>
#include "px_common_new/string_util.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/time_util.h"
#include "px_common_new/monitors.h"
#include "px_common_new/image.h"
#include "px_common_new/math_helper.h"
#include "px_common_new/win32/win_helper.h"
#include "px_capture_new/capture_message.h"
#include "px_render/architecture/events/render_event.h"
#include "gdi_capture_source.h"
#include "px_common_new/win32/d3d_debug_helper.h"


namespace px
{

    void GdiCapture::DcDeleter::operator()(
        std::remove_pointer_t<HDC>* dc) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): typed Win32 HDC RAII boundary
        if (dc) {
            DeleteDC(dc);
        }
    }

    void GdiCapture::BitmapDeleter::operator()(
        std::remove_pointer_t<HBITMAP>* bitmap) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): typed Win32 HBITMAP RAII boundary
        if (bitmap) {
            DeleteObject(bitmap);
        }
    }

    struct MonitorGeometry final {
        std::wstring target_name;
        bool found = false;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    // NOLINTNEXTLINE(gammaray-raw-pointer-boundary): synchronous Win32 enumeration ABI
    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
        auto& geometry = *reinterpret_cast<MonitorGeometry*>(
            dwData); // NOLINT(gammaray-raw-pointer-boundary): synchronous LPARAM boundary
        MONITORINFOEX monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFOEX);
        GetMonitorInfoW(hMonitor, &monitorInfo);

        auto it_mon_name = std::wstring(monitorInfo.szDevice);
        if (it_mon_name != geometry.target_name) {
            return TRUE;
        }

        geometry.found = true;
        geometry.left = lprcMonitor->left;
        geometry.top = lprcMonitor->top;
        geometry.right = lprcMonitor->right;
        geometry.bottom = lprcMonitor->bottom;

        //LOGI("screen_width: {}, screen_height: {}", screen_width, screen_height);
        return TRUE;
    }

    std::shared_ptr<GdiCapture> GdiCapture::Make(
        const std::shared_ptr<GdiCaptureSource>& owner,
        const CaptureMonitorInfo& my_monitor_info) {
        return std::make_shared<GdiCapture>(owner, my_monitor_info);
    }

    GdiCapture::GdiCapture(
        const std::shared_ptr<GdiCaptureSource>& owner,
        const CaptureMonitorInfo& my_monitor_info)
        : DesktopCaptureSource(my_monitor_info) {
        owner_ = owner;
        fps_stat_ = std::make_shared<FpsStat>();
        monitor_name_ = StringUtil::ToWString(my_monitor_info_.name_);
        is_primary_monitor_ = my_monitor_info_.primary_;
        LOGI("GdiCapture my monitor info: {}", my_monitor_info.Dump());
    }

    GdiCapture::~GdiCapture() {
       StopCapture();
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
        Exit();

        if (!RefreshMonitorGeometry()) {
            width_ = 0;
            height_ = 0;
            LOGE("monitor is no longer available: {}", my_monitor_info_.name_);
            return false;
        }
        if (width_ <= 0 || height_ <= 0) {
            LOGE("monitor size error: {}x{}", width_, height_);
            return false;
        }

        //screen_dc_ = GetDC(NULL); // 整个虚拟屏幕的设备上下文, GetDC 是采集整个虚拟屏幕的画面,GDI 作为托底采集,就采集整个虚拟桌面就可以
        screen_dc_.reset(CreateDCW(nullptr, monitor_name_.c_str(), nullptr, nullptr)); // CreateDC 可以采集特定屏幕的画面
        if (!screen_dc_) {
            LOGW("GdiCapture GetDC failed.");
            return false;
        }

        memory_dc_.reset(CreateCompatibleDC(screen_dc_.get()));
        if (!memory_dc_) {
            LOGW("GdiCapture CreateCompatibleDC failed.");
            return false;
        }

        if (SetStretchBltMode(memory_dc_.get(), COLORONCOLOR) == 0) { // 使用 COLORONCOLOR 可以提高图像缩放的速度，适合在不需要透明效果的情况下使用。
            LOGW("SetStretchBltMode failed.");
        }

        // 创建兼容位图
        bit_map_.reset(CreateCompatibleBitmap(
            screen_dc_.get(), my_monitor_info_.Width(), my_monitor_info_.Height()));
        if (!bit_map_) {
            LOGW("CreateCompatibleBitmap failed.");
            return false;
        }

        // 选择位图到内存 DC 中
        SelectObject(memory_dc_.get(), bit_map_.get());
        init_success_ = true;
        LOGI("GdiCapture Init OK.");
        return true;
    }

    bool GdiCapture::RefreshMonitorGeometry() {
        MonitorGeometry geometry{.target_name = monitor_name_};
        EnumDisplayMonitors(
            nullptr, nullptr, MonitorEnumProc,
            reinterpret_cast<LPARAM>(std::addressof(geometry)));
        if (!geometry.found) {
            return false;
        }
        const int screen_width = geometry.right - geometry.left;
        const int screen_height = geometry.bottom - geometry.top;
        if ((screen_width != width_ && width_ > 0) ||
            (screen_height != height_ && height_ > 0)) {
            reinit_ = true;
            LOGW("GDI Screen size changed, origin: {}x{}, now: {}x{}",
                 width_, height_, screen_width, screen_height);
        }
        left_ = geometry.left;
        top_ = geometry.top;
        right_ = geometry.right;
        bottom_ = geometry.bottom;
        width_ = screen_width;
        height_ = screen_height;
        return true;
    }

    bool GdiCapture::IsInitSuccess() {
        return init_success_;
    }

    bool GdiCapture::Exit() {
        init_success_ = false;

        // Destroy the memory DC before its selected bitmap. DeleteObject fails
        // while a bitmap is still selected into a live DC.
        memory_dc_.reset();
        bit_map_.reset();
        screen_dc_.reset();

        return true;
    }

    bool GdiCapture::CaptureNextFrame() {
        // 复制整个虚拟屏幕的内容到内存 DC
        if (!memory_dc_ || !screen_dc_ || !bit_map_) {
            return false;
        }
        const bool copied = BitBlt(
            memory_dc_.get(), 0, 0, my_monitor_info_.Width(),
            my_monitor_info_.Height(), screen_dc_.get(), 0, 0,
            SRCCOPY) != FALSE;
        if (!copied) {
            LOGW("GDI BitBlt failed for monitor: {}", my_monitor_info_.name_);
        }
        BITMAP bmp{};
        if (GetObject(bit_map_.get(), sizeof(BITMAP), &bmp) == 0) {
            LOGW("GDI GetObject failed for monitor: {}", my_monitor_info_.name_);
            return false;
        }
        
        BITMAPINFOHEADER bi{};
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

        if (!data_ptr->DataAddr()) {
            LOGE("bitmap data address is null, size: {}", dwBmpSize);
            return false;
        }

        int ret = GetDIBits(
            memory_dc_.get(), bit_map_.get(), 0, (UINT)bmp.bmHeight,
            data_ptr->DataAddr(),
            reinterpret_cast<BITMAPINFO*>(
                std::addressof(bi)), // NOLINT(gammaray-raw-pointer-boundary): Win32 BITMAPINFO ABI boundary
            DIB_RGB_COLORS);
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
        const auto owner = owner_.lock();
        if (!owner) {
            return false;
        }
        auto mon_index_res = owner->MonitorIndexByName(my_monitor_info_.name_);
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

        if (owner->IsEnabled()) {
            auto event = std::make_shared<CapturedVideoFrameEvent>();
            event->frame_ = cap_video_frame;
            owner->EmitEvent(event);
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

        return copied;
    }

    void GdiCapture::Start() {
        LOGI("GdiCapture::Start() stop flag : {}", stop_flag_.load());
        const auto weak_capture = weak_from_this();
        std::scoped_lock lock(capture_thread_mutex_);
        if (capture_thread_.joinable()) {
            LOGW("GdiCapture::Start ignored: capture worker is already active");
            return;
        }
        capture_thread_ = std::jthread(
            [weak_capture](std::stop_token stop_token) {
                CaptureWorker(weak_capture, stop_token);
            });
    }

    void GdiCapture::CaptureWorker(
        const std::weak_ptr<GdiCapture>& weak_capture,
        std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            const auto capture = weak_capture.lock();
            if (!capture || capture->stop_flag_) {
                break;
            }
            capture->CaptureIteration(stop_token);
        }
    }

    void GdiCapture::CaptureIteration(std::stop_token stop_token) {
        if (stop_token.stop_requested() || stop_flag_) {
            return;
        }
        const auto owner = owner_.lock();
        if (pausing_ || !owner || owner->HasNoConnectedClients()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(17));
            return;
        }

        // check display size
        if (!RefreshMonitorGeometry()) {
            reinit_ = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(17));
            return;
        }

        if (!WinHelper::InputDesktopSelected() || reinit_) {
            if (!WinHelper::SelectInputDesktop()) {
                LOGE("GDI capture SelectInputDesktop error.");
            }
            // 切换桌面后，需要重新初始化 GDI相关
            Exit();
            if (!stop_token.stop_requested() && !stop_flag_) {
                Init();
                owner->RequestKeyFrame();
            }

            reinit_ = false;
        }
        if (!stop_token.stop_requested() && !stop_flag_ &&
            !CaptureNextFrame()) {
            // Keep the legacy fallback frame delivery semantics, but do not
            // spin at full CPU when the interactive desktop is unavailable.
            std::this_thread::sleep_for(std::chrono::milliseconds(17));
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
        if (const auto owner = owner_.lock()) {
            owner->RequestKeyFrame();
        }
    }

    void GdiCapture::StopCapture() {
        stop_flag_ = true;
        std::jthread worker;
        {
            std::scoped_lock lock(capture_thread_mutex_);
            worker = std::move(capture_thread_);
        }
        if (worker.joinable()) {
            worker.request_stop();
            if (worker.get_id() == std::this_thread::get_id()) {
                PxAsyncRuntime::DeferJoin(std::move(worker));
            }
            else {
                worker.join();
            }
        }
        Exit();
    }

    void GdiCapture::RefreshScreen() {
        DesktopCaptureSource::RefreshScreen();
        used_cache_times_ = 0;
    }

    bool GdiCapture::IsPrimaryMonitor() {
        return is_primary_monitor_;
    }

    int GdiCapture::GetCapturingFps() {
        return fps_stat_->value();
    }

} // tc
