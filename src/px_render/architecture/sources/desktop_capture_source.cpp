#include "desktop_capture_source.h"
#include "px_common_new/log.h"
#include "px_render/architecture/events/render_event.h"
#include <Shlobj.h>
#include <px_common_new/string_util.h>

namespace px
{

    DesktopCaptureSource::DesktopCaptureSource(const CaptureMonitorInfo& my_monitor_info) {
        my_monitor_info_ = my_monitor_info;
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    void DesktopCaptureSource::SetCaptureFps(int fps) {
        capture_fps_ = fps;
    }

    void DesktopCaptureSource::RefreshScreen() {
        refresh_screen_ = true;
    }

    bool DesktopCaptureSource::IsPrimaryMonitor() {
        return false;
    }

    std::vector<int32_t> DesktopCaptureSource::GetCaptureGaps() {
        return capture_gaps_.ToVector();
    }

    void DesktopCaptureSource::TryWakeOs() {
        // mock 1 pixel mouse move
        {
            INPUT input = {0};
            input.type = INPUT_MOUSE;
            input.mi.dx = 1;
            input.mi.dy = 1;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(1, &input, sizeof(INPUT));
        }
        {
            INPUT input = {0};
            input.type = INPUT_MOUSE;
            input.mi.dx = -1;
            input.mi.dy = -1;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(1, &input, sizeof(INPUT));
        }
        // mock a keyboard event
        //INPUT inputs[2] = {0};
        //inputs[0].type = INPUT_KEYBOARD;
        //inputs[0].ki.wVk = VK_SHIFT;
        //inputs[0].ki.dwFlags = 0;

        //inputs[1].type = INPUT_KEYBOARD;
        //inputs[1].ki.wVk = VK_SHIFT;
        //inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        //SendInput(2, inputs, sizeof(INPUT));
    }

    void DesktopCaptureSource::On16MilliSecond() {

    }

    void DesktopCaptureSource::On33MilliSecond() {

    }

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
        // Synchronous Win32 enumeration callback boundary; IsThisMonitorExist
        // retains the shared owner for the complete call.
        auto& capture = *reinterpret_cast<DesktopCaptureSource*>(dwData); // NOLINT(gammaray-raw-pointer-boundary)
        MONITORINFOEX monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFOEX);
        GetMonitorInfoW(hMonitor, &monitorInfo);
        const auto it_mon_name = std::wstring(monitorInfo.szDevice);
        capture.monitors_name_.push_back(it_mon_name);
        return TRUE;
    }

    bool DesktopCaptureSource::IsThisMonitorExist() {
        EnumDisplayMonitors(
            nullptr, nullptr, MonitorEnumProc,
            reinterpret_cast<LPARAM>(this)); // NOLINT(gammaray-raw-pointer-boundary): synchronous Win32 callback boundary
        for (const auto& n : monitors_name_) {
            const auto u8_name = StringUtil::ToUTF8(n);
            if (u8_name == my_monitor_info_.name_) {
                return true;
            }
        }
        return false;
    }

}
