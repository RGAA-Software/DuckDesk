//
// Created by RGAA  on 2024/2/12.
//

#ifndef TC_APPLICATION_CONTROL_EVENT_REPLAYER_WIN_H
#define TC_APPLICATION_CONTROL_EVENT_REPLAYER_WIN_H

#include <memory>
#include "px_message.pb.h"
#include "px_capture/capture_message.h"

namespace px
{

    class WinEventReplayer {
    public:
        void HandleKeyEvent(const px::KeyEvent& event);
        void HandleMouseEvent(const px::MouseEvent& event);
        void HandleFocusOutEvent();
        void ReplayMouseEvent(const std::string& monitor_name, float x_ratio, float y_ratio, int buttons, int data);
        void UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& msg);
        void SimulateCtrlWinShiftB();

    private:
        bool IsKeyPermitted(uint32_t vk);
        void ResetKey();
        void ReplayKeyEvent(uint16_t scancode, bool extend, const px::KeyEvent& event);
        void MockKeyEvent(uint16_t scancode);
        void ReplayVirtualDesktopMouseEvent(float x_ratio, float y_ratio, int buttons, int data);
        void SendMouseEvent(int x, int y, int buttons, int data);
        void UpdateHeldButtons(int buttons);
        // 由绝对目标点在 server 侧换算相对 MOVE(忽略 client delta_*)
        bool InjectServerRelFromAbs(int abs_x, int abs_y, int buttons, bool log_it);

    private:
        // capturing monitors
        std::vector<CaptureMonitorInfo> monitors_;
        VirtualDesktopBoundRectangleInfo virtual_desktop_bound_rectangle_info_;

        bool current_key_status_[256]{};
        bool control_pressed_{};
        bool menu_pressed_{};
        bool delete_pressed_{};
        bool shift_pressed_{};
        bool win_pressed_{};
        // 跟踪远端已按下的鼠标键,用于拖动过程中打相对移动日志
        bool left_held_ = false;
        bool middle_held_ = false;
        bool right_held_ = false;
    };
}


#endif //TC_APPLICATION_CONTROL_EVENT_REPLAYER_WIN_H
