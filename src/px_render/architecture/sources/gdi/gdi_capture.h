#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <mutex>
#include <stop_token>
#include <type_traits>
#include "px_common_new/monitors.h"
#include "px_common_new/fps_stat.h"
#include "px_render/architecture/sources/desktop_capture_source.h"

namespace px
{

    class Data;
    class GdiCaptureSource;

    class GdiCapture : public DesktopCaptureSource,
                       public std::enable_shared_from_this<GdiCapture> {
    public:
        static std::shared_ptr<GdiCapture> Make(
            const std::shared_ptr<GdiCaptureSource>& owner,
            const CaptureMonitorInfo& my_monitor_info);
        explicit GdiCapture(
            const std::shared_ptr<GdiCaptureSource>& owner,
            const CaptureMonitorInfo& my_monitor_info);
        virtual ~GdiCapture();
        bool Init() override;
        bool StartCapture() override;
        bool PauseCapture() override;
        void ResumeCapture() override;
        void StopCapture() override;
        void RefreshScreen() override;
        bool IsPrimaryMonitor() override;
        bool IsInitSuccess() override;
        int GetCapturingFps() override;

    private:
        bool InitInternal();
        bool RefreshMonitorGeometry();
        void Start();
        bool Exit();
        static void CaptureWorker(
            const std::weak_ptr<GdiCapture>& weak_capture,
            std::stop_token stop_token);
        void CaptureIteration(std::stop_token stop_token);

        bool CaptureNextFrame();
        int64_t GetFrameIndex();

    public:
        std::wstring monitor_name_;
        int left_ = 0;
        int top_ = 0;
        int right_ = 0;
        int bottom_ = 0;
        int width_ = 0;
        int height_ = 0;
        bool reinit_ = false;

    private:
        struct DcDeleter final {
            void operator()(std::remove_pointer_t<HDC>* dc) const noexcept; // NOLINT(gammaray-raw-pointer-boundary): typed Win32 HDC RAII boundary
        };
        struct BitmapDeleter final {
            void operator()(
                std::remove_pointer_t<HBITMAP>* bitmap) const noexcept; // NOLINT(gammaray-raw-pointer-boundary): typed Win32 HBITMAP RAII boundary
        };
        using UniqueDc =
            std::unique_ptr<std::remove_pointer_t<HDC>, DcDeleter>;
        using UniqueBitmap =
            std::unique_ptr<std::remove_pointer_t<HBITMAP>, BitmapDeleter>;

        std::weak_ptr<GdiCaptureSource> owner_;
        std::atomic_bool init_success_ = false;
        std::atomic<bool> stop_flag_ = false;
        std::mutex capture_thread_mutex_;
        std::jthread capture_thread_;
        int64_t monitor_frame_index_ = 0;
        int used_cache_times_ = 0;
        std::shared_ptr<FpsStat> fps_stat_ = nullptr;
        int64_t last_captured_timestamp_ = 0;

        UniqueBitmap bit_map_;
        UniqueDc screen_dc_;
        UniqueDc memory_dc_;
    };
}
