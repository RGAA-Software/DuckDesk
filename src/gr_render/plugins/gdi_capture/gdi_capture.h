#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include "tc_common_new/monitors.h"
#include "tc_common_new/fps_stat.h"
#include "gr_render/plugins/plugin_desktop_capture.h"

namespace tc
{

    class Data;
    class GdiCapturePlugin;

    class GdiCapture : public PluginDesktopCapture {
    public:
        static std::shared_ptr<GdiCapture> Make(GdiCapturePlugin* plugin, const CaptureMonitorInfo& my_monitor_info);
        explicit GdiCapture(GdiCapturePlugin* plugin, const CaptureMonitorInfo& my_monitor_info);
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
        void Start();
        bool Exit();
        void Capture();

        bool CaptureNextFrame();
        int64_t GetFrameIndex();

    public:
        std::wstring mon_name_;
        int left_ = 0;
        int top_ = 0;
        int right_ = 0;
        int bottom_ = 0;
        int width_ = 0;
        int height_ = 0;
        bool reinit_ = false;

    private:
        GdiCapturePlugin* plugin_ = nullptr;
        bool init_success_ = false;
        std::atomic<bool> stop_flag_ = false;
        std::thread capture_thread_;
        int64_t monitor_frame_index_ = 0;
        int used_cache_times_ = 0;
        std::shared_ptr<FpsStat> fps_stat_ = nullptr;
        int64_t last_captured_timestamp_ = 0;

        HBITMAP bit_map_ = nullptr;
        HDC screen_dc_ = nullptr;
        HDC memory_dc_ = nullptr;
    };
}