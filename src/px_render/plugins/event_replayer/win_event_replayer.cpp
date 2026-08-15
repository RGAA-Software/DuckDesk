//
// Created by RGAA  on 2024/2/12.
//

#include "win_event_replayer.h"
#include "tc_message.pb.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "px_render/plugin_interface/gr_monitor_capture_plugin.h"
#include <cstdio>
#include <string>
#include <atomic>
#include <memory>

namespace tc
{
    namespace {
        const uint32_t kExtendedKeys[] = {
            VK_DELETE, VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN, VK_NUMLOCK,
            VK_RCONTROL, VK_RMENU, VK_RETURN, VK_DIVIDE, VK_LWIN,
            VK_RWIN, VK_HOME, VK_PRIOR, VK_NEXT, VK_END, VK_INSERT,
        };

        bool IsPureMouseMove(int buttons) {
            return buttons == 0 || buttons == ButtonFlag::kMouseMove;
        }

        std::string DescribeButtonFlags(int buttons) {
            std::string s;
            auto append = [&](const char* name) {
                if (!s.empty()) s += "|";
                s += name;
            };
            if (buttons & ButtonFlag::kMouseMove) append("MOVE");
            if (buttons & ButtonFlag::kLeftMouseButtonDown) append("LDOWN");
            if (buttons & ButtonFlag::kLeftMouseButtonUp) append("LUP");
            if (buttons & ButtonFlag::kMiddleMouseButtonDown) append("MDOWN");
            if (buttons & ButtonFlag::kMiddleMouseButtonUp) append("MUP");
            if (buttons & ButtonFlag::kRightMouseButtonDown) append("RDOWN");
            if (buttons & ButtonFlag::kRightMouseButtonUp) append("RUP");
            if (buttons & ButtonFlag::kMouseEventWheel) append("WHEEL");
            if (buttons & ButtonFlag::kMouseEventHWheel) append("HWHEEL");
            if (s.empty()) s = "NONE";
            return s + "(" + std::to_string(buttons) + ")";
        }

        std::string DescribeDwFlags(DWORD flags) {
            std::string s;
            auto append = [&](const char* name) {
                if (!s.empty()) s += "|";
                s += name;
            };
            if (flags & MOUSEEVENTF_MOVE) append("MOVE");
            if (flags & MOUSEEVENTF_LEFTDOWN) append("LEFTDOWN");
            if (flags & MOUSEEVENTF_LEFTUP) append("LEFTUP");
            if (flags & MOUSEEVENTF_RIGHTDOWN) append("RIGHTDOWN");
            if (flags & MOUSEEVENTF_RIGHTUP) append("RIGHTUP");
            if (flags & MOUSEEVENTF_MIDDLEDOWN) append("MIDDLEDOWN");
            if (flags & MOUSEEVENTF_MIDDLEUP) append("MIDDLEUP");
            if (flags & MOUSEEVENTF_WHEEL) append("WHEEL");
            if (flags & MOUSEEVENTF_HWHEEL) append("HWHEEL");
            if (flags & MOUSEEVENTF_ABSOLUTE) append("ABSOLUTE");
            if (flags & MOUSEEVENTF_VIRTUALDESK) append("VIRTUALDESK");
            if (s.empty()) s = "NONE";
            char hex[16];
            std::snprintf(hex, sizeof(hex), "%08lX", static_cast<unsigned long>(flags));
            return s + "(0x" + hex + ")";
        }

        // [LAT-input] 输入注入(SendInput)计时统计
        std::atomic<uint64_t> g_sendinput_calls{0};
        std::atomic<uint64_t> g_replay_events{0};
        std::atomic<uint64_t> g_replay_us_sum{0};
        std::atomic<uint64_t> g_replay_us_max{0};
        std::atomic<uint64_t> g_replay_calls_sum{0};

        void DumpReplayLatencyIfDue() {
            static std::atomic<uint64_t> s_last_dump_us{0};
            auto now = TimeUtil::GetCurrentTimePointUS();
            auto last = s_last_dump_us.load();
            if (now - last < 5000000) {
                return;
            }
            if (!s_last_dump_us.compare_exchange_weak(last, now)) {
                return;
            }
            auto ev = g_replay_events.exchange(0);
            auto sum = g_replay_us_sum.exchange(0);
            auto mx = g_replay_us_max.exchange(0);
            auto calls = g_replay_calls_sum.exchange(0);
            LOGI("[LAT-input] inject events={} avg_us={} max_us={} sendinput_calls={}",
                 ev, ev > 0 ? (sum / ev) : 0, mx, calls);
        }
    }

    // 返回是否注入成功;失败时带 GetLastError
    bool WinSendEvent(INPUT* input) {
        if (!input) {
            LOGE("[InputReplay] WinSendEvent null INPUT");
            return false;
        }
        ++g_sendinput_calls;
        SetLastError(0);
        if (SendInput(1, input, sizeof(INPUT)) == 1) {
            return true;
        }
        const DWORD first_err = GetLastError();
        HDESK desk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (!desk) {
            LOGE("[InputReplay] SendInput failed err={}, OpenInputDesktop failed err={}",
                 first_err, GetLastError());
            return false;
        }
        if (!SetThreadDesktop(desk)) {
            LOGE("[InputReplay] SendInput failed err={}, SetThreadDesktop failed err={}",
                 first_err, GetLastError());
            CloseDesktop(desk);
            return false;
        }
        SetLastError(0);
        const bool ok = SendInput(1, input, sizeof(INPUT)) == 1;
        if (!ok) {
            LOGE("[InputReplay] SendInput retry on input desktop failed err={} (first_err={})",
                 GetLastError(), first_err);
        } else {
            LOGW("[InputReplay] SendInput succeeded after switching to input desktop (first_err={})",
                 first_err);
        }
        CloseDesktop(desk);
        return ok;
    }

    INPUT GenerateScanCodeInput(uint16_t scancode, bool down, bool extend) {
        INPUT evt;
        memset(&evt, 0, sizeof(evt));
        evt.type = INPUT_KEYBOARD;
        evt.ki.wVk = 0;
        evt.ki.dwFlags = KEYEVENTF_SCANCODE;

        if (!down) {
            evt.ki.dwFlags |= KEYEVENTF_KEYUP;
        }
        if (extend) {
            evt.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }

        evt.ki.wScan = scancode;
        evt.ki.dwExtraInfo = 0;
        evt.ki.time = 0;
        if (evt.ki.wScan == 0x45) {
            evt.ki.dwFlags &= ~KEYEVENTF_SCANCODE;
            if (evt.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) {
                evt.ki.wVk = VK_NUMLOCK;
            } else {
                evt.ki.wVk = VK_PAUSE;
            }
        }
        if (evt.ki.wScan == 0x3a) {
            evt.ki.dwFlags &= ~KEYEVENTF_SCANCODE;
            evt.ki.wVk = VK_CAPITAL;
        }
        return evt;
    }

    void WinEventReplayer::HandleKeyEvent(const tc::KeyEvent& event) {
        bool down = event.down();
        uint32_t vk_code = event.key_code();
        if (vk_code > 255) {
            LOGE("[InputReplay] invalid vk: {}", vk_code);
            return;
        }

        current_key_status_[vk_code] = down;
        if (!IsKeyPermitted(vk_code)) {
            LOGW("[InputReplay] key blocked by policy, vk=0x{:x} down={}", vk_code, down);
            current_key_status_[vk_code] =!down;
            return;
        }

        if (vk_code == VK_CONTROL || vk_code == VK_RCONTROL || vk_code == VK_LCONTROL) {
            control_pressed_ = down;
        }
        if (vk_code == VK_MENU || vk_code == VK_RMENU || vk_code == VK_LMENU) {
            menu_pressed_ = down;
        }
        if (vk_code == VK_DELETE) {
            delete_pressed_ = down;
        }
        if (vk_code == VK_SHIFT || vk_code == VK_RSHIFT || vk_code == VK_LSHIFT) {
            shift_pressed_ = down;
        }
        if (vk_code == VK_LWIN || vk_code == VK_RWIN) {
            win_pressed_ = down;
        }

        if(control_pressed_ && menu_pressed_ && delete_pressed_ && !shift_pressed_ && !win_pressed_) {
            LOGW("[InputReplay] swallow Ctrl+Alt+Delete combo, vk=0x{:x} down={}", vk_code, down);
            return;
        }
        UINT vsc = MapVirtualKey(vk_code, MAPVK_VK_TO_VSC);
        bool extend = false;
        for (size_t j = 0; j < sizeof(kExtendedKeys) / sizeof(UINT32); j++) {
            if (kExtendedKeys[j] == vk_code) {
                extend = true;
                break;
            }
        }
        LOGI("[InputReplay] key vk=0x{:x} down={} scancode=0x{:x} extend={} numLock={} capsLock={} check={}",
             vk_code, down, vsc, extend, event.num_lock_status(), event.caps_lock_status(),
             static_cast<int>(event.status_check()));
        ReplayKeyEvent(vsc, extend, event);
    }

    bool WinEventReplayer::IsKeyPermitted(uint32_t vk) {
        return true;
    }

    void WinEventReplayer::ResetKey() {
        for(int i = 0; i < sizeof(current_key_status_) / sizeof(*current_key_status_);++i) {
            if (current_key_status_[i]) {
                tc::KeyEvent event;
                event.set_down(false);
                event.set_key_code(i);
                HandleKeyEvent(event);
            }
        }
    }

    void WinEventReplayer::ReplayKeyEvent(uint16_t scancode, bool extend, const tc::KeyEvent& event) {
        INPUT evt = GenerateScanCodeInput(scancode, event.down(), extend);
        short num_lock_status = event.num_lock_status();
        short curr_num_lock_status = GetKeyState(VK_NUMLOCK);

        if (evt.ki.wVk == VK_NUMLOCK) {
            LOGI("numlock status : {}, current status: {}", num_lock_status, curr_num_lock_status);
            if (curr_num_lock_status != num_lock_status) {
                LOGI("NumLock, NO need to send event.");
                return;
            }
        } else {
            if (event.down() && num_lock_status != curr_num_lock_status && event.status_check() == KeyEvent::kCheckNumLock) {
                MockKeyEvent(0x45);
            }
        }

        short caps_lock_status = event.caps_lock_status();
        short curr_caps_lock_status = GetKeyState(VK_CAPITAL);
        if (scancode == 0x3a) {
            if (curr_caps_lock_status == caps_lock_status) {
                return;
            }
        } else {
            if (event.down() && caps_lock_status != curr_caps_lock_status && event.status_check() == KeyEvent::kCheckCapsLock) {
                MockKeyEvent(0x3a);
            }
        }
        WinSendEvent(&evt);
    }

    void WinEventReplayer::MockKeyEvent(uint16_t scancode) {
        INPUT down = GenerateScanCodeInput(scancode, true, true);
        WinSendEvent(&down);
        INPUT up = GenerateScanCodeInput(scancode, false, true);
        WinSendEvent(&up);
    }

    void WinEventReplayer::UpdateHeldButtons(int buttons) {
        if (buttons & ButtonFlag::kLeftMouseButtonDown) left_held_ = true;
        if (buttons & ButtonFlag::kLeftMouseButtonUp) left_held_ = false;
        if (buttons & ButtonFlag::kMiddleMouseButtonDown) middle_held_ = true;
        if (buttons & ButtonFlag::kMiddleMouseButtonUp) middle_held_ = false;
        if (buttons & ButtonFlag::kRightMouseButtonDown) right_held_ = true;
        if (buttons & ButtonFlag::kRightMouseButtonUp) right_held_ = false;
    }

    bool WinEventReplayer::InjectServerRelFromAbs(int abs_x, int abs_y, int buttons, bool log_it) {
        const int far_left = virtual_desktop_bound_rectangle_info_.far_left_;
        const int far_top = virtual_desktop_bound_rectangle_info_.far_top_;
        const int far_right = virtual_desktop_bound_rectangle_info_.far_right_;
        const int far_bottom = virtual_desktop_bound_rectangle_info_.far_bottom_;
        const int desk_w = far_right - far_left - 1;
        const int desk_h = far_bottom - far_top - 1;
        if (desk_w <= 0 || desk_h <= 0) {
            return false;
        }
        const int dest_x = far_left + static_cast<int>((static_cast<long long>(abs_x) * desk_w) / 65535);
        const int dest_y = far_top + static_cast<int>((static_cast<long long>(abs_y) * desk_h) / 65535);
        POINT cur{};
        if (!GetCursorPos(&cur)) {
            return false;
        }
        int diff_x = dest_x - cur.x;
        int diff_y = dest_y - cur.y;
        if (diff_x == 0 && diff_y == 0) {
            return false;
        }

        // 单次相对位移不超过鼠标加速第一阈值,避免系统加倍
        int threshold = 6;
        int mouse_params[3] = {0, 0, 0};
        if (SystemParametersInfo(SPI_GETMOUSE, 0, mouse_params, 0) && mouse_params[0] > 0) {
            threshold = mouse_params[0];
        }

        auto send_rel = [&](int rx, int ry) {
            INPUT evt{};
            evt.type = INPUT_MOUSE;
            evt.mi.dx = rx;
            evt.mi.dy = ry;
            evt.mi.dwFlags = MOUSEEVENTF_MOVE;
            evt.mi.dwExtraInfo = 0;
            evt.mi.mouseData = 0;
            evt.mi.time = 0;
            const bool ok = WinSendEvent(&evt);
            if (!log_it) {
                return;
            }
            if (ok) {
                LOGI("[InputReplay] SendInput ok {} tag=server-rel dwFlags={} rel=({},{}) dest=({},{}) cur=({},{})",
                     DescribeButtonFlags(buttons), DescribeDwFlags(MOUSEEVENTF_MOVE),
                     rx, ry, dest_x, dest_y, cur.x, cur.y);
            } else {
                LOGE("[InputReplay] SendInput FAIL {} tag=server-rel rel=({},{})",
                     DescribeButtonFlags(buttons), rx, ry);
            }
        };

        const int count_x = std::abs(diff_x) / threshold;
        const int count_y = std::abs(diff_y) / threshold;
        const int unit_x = diff_x > 0 ? threshold : -threshold;
        const int unit_y = diff_y > 0 ? threshold : -threshold;
        if (count_x > count_y) {
            for (int i = 0; i < count_y; ++i) send_rel(unit_x, unit_y);
            for (int i = 0; i < count_x - count_y; ++i) send_rel(unit_x, 0);
        } else {
            for (int i = 0; i < count_x; ++i) send_rel(unit_x, unit_y);
            for (int i = 0; i < count_y - count_x; ++i) send_rel(0, unit_y);
        }
        const int rem_x = diff_x % threshold;
        const int rem_y = diff_y % threshold;
        if (rem_x != 0 || rem_y != 0) {
            send_rel(rem_x, rem_y);
        }
        return true;
    }

    void WinEventReplayer::HandleMouseEvent(const tc::MouseEvent& event) {
        float x_ratio = event.x_ratio();
        float y_ratio = event.y_ratio();
        std::string monitor_name = event.monitor_name();
        int button = event.button();
        int data = event.data();
        const bool dragging = left_held_ || middle_held_ || right_held_;
        // 纯移动不打日志;按键/滚轮/按住拖动要打
        if (!IsPureMouseMove(button) || data != 0 || dragging) {
            LOGI("[InputReplay] recv mouse btn={} data={} pressed={} released={} ratio=({:.4f},{:.4f}) monitor={} held=L{}M{}R{}",
                 DescribeButtonFlags(button), data, event.pressed(), event.released(),
                 x_ratio, y_ratio, monitor_name,
                 left_held_, middle_held_, right_held_);
        }
        ReplayMouseEvent(monitor_name, x_ratio, y_ratio, button, data);
    }

    void WinEventReplayer::ReplayMouseEvent(const std::string& monitor_name, float x_ratio, float y_ratio, int buttons, int data) {

        //if (kVirtualDesktopNameSign == monitor_name) {
        //    ReplayVirtualDesktopMouseEvent(x_ratio, y_ratio, buttons, data);
        //    return;
        //}

        if (monitors_.empty()) {
            LOGE("[InputReplay] drop mouse {}, no capturing monitor info (monitor={})",
                 DescribeButtonFlags(buttons), monitor_name);
            return;
        }
        auto func_find_monitor = [&]() -> CaptureMonitorInfo {
            for (auto& mon : monitors_) {
                if (std::string(mon.name_) == monitor_name) {
                    return mon;
                }
            }
            return CaptureMonitorInfo{};
        };

        auto target_monitor = func_find_monitor();
        if (!target_monitor.Valid()) {
            std::string known;
            for (const auto& mon : monitors_) {
                if (!known.empty()) known += ",";
                known += mon.name_;
            }
            LOGE("[InputReplay] drop mouse {}, invalid monitor='{}' known=[{}] count={}",
                 DescribeButtonFlags(buttons), monitor_name, known, monitors_.size());
            return;
        }
        int x = (
            x_ratio * target_monitor.Width()
            +
            std::abs(target_monitor.left_ - virtual_desktop_bound_rectangle_info_.far_left_)
            ) * 65535
            /
            (virtual_desktop_bound_rectangle_info_.far_right_ - virtual_desktop_bound_rectangle_info_.far_left_ - 1);

        int y = (
            y_ratio * target_monitor.Height()
            +
            std::abs(target_monitor.top_ - virtual_desktop_bound_rectangle_info_.far_top_)
            ) * 65535
            /
            (virtual_desktop_bound_rectangle_info_.far_bottom_ - virtual_desktop_bound_rectangle_info_.far_top_ - 1);

        const bool dragging = left_held_ || middle_held_ || right_held_;
        if (!IsPureMouseMove(buttons) || data != 0 || dragging) {
            LOGI("[InputReplay] map mouse {} -> abs=({},{}) monitor={} size={}x{} desk=[{},{}]-[{},{}]",
                 DescribeButtonFlags(buttons), x, y, monitor_name,
                 target_monitor.Width(), target_monitor.Height(),
                 virtual_desktop_bound_rectangle_info_.far_left_,
                 virtual_desktop_bound_rectangle_info_.far_top_,
                 virtual_desktop_bound_rectangle_info_.far_right_,
                 virtual_desktop_bound_rectangle_info_.far_bottom_);
        }
        SendMouseEvent(x, y, buttons, data);
    }

    void WinEventReplayer::UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& msg) {
        monitors_ = msg.monitors_;
        virtual_desktop_bound_rectangle_info_ = msg.virtual_desktop_bound_rectangle_info_;
        std::string names;
        for (const auto& mon : monitors_) {
            if (!names.empty()) names += ",";
            names += mon.name_;
        }
        LOGI("[InputReplay] UpdateCaptureMonitorInfo count={} monitors=[{}] desk=[{},{}]-[{},{}]",
             monitors_.size(), names,
             virtual_desktop_bound_rectangle_info_.far_left_,
             virtual_desktop_bound_rectangle_info_.far_top_,
             virtual_desktop_bound_rectangle_info_.far_right_,
             virtual_desktop_bound_rectangle_info_.far_bottom_);
    }

    void WinEventReplayer::ReplayVirtualDesktopMouseEvent(float x_ratio, float y_ratio, int buttons, int data) {
        int x = x_ratio * 65535;
        int y = y_ratio * 65535;
        SendMouseEvent(x, y, buttons, data);
    }

    void WinEventReplayer::SendMouseEvent(int x, int y, int buttons, int data) {
        // [LAT-input] 计时本次鼠标注入(相对+绝对 SendInput)耗时与次数;RAII 覆盖所有 return 路径
        const uint64_t lat_beg_us = TimeUtil::GetCurrentTimePointUS();
        const uint64_t lat_calls_before = g_sendinput_calls.load();
        auto lat_guard = std::shared_ptr<void>(nullptr, [lat_beg_us, lat_calls_before](void*) {
            const uint64_t us = TimeUtil::GetCurrentTimePointUS() - lat_beg_us;
            const uint64_t calls = g_sendinput_calls.load() - lat_calls_before;
            ++g_replay_events;
            g_replay_us_sum += us;
            auto prev = g_replay_us_max.load();
            while (us > prev && !g_replay_us_max.compare_exchange_weak(prev, us)) {}
            g_replay_calls_sum += calls;
            DumpReplayLatencyIfDue();
        });

        const bool dragging = left_held_ || middle_held_ || right_held_;
        const bool significant = !IsPureMouseMove(buttons) || data != 0 || dragging;
        auto inject_abs = [&](DWORD dw_flags, int mouse_data, const char* tag) {
            INPUT evt{};
            evt.type = INPUT_MOUSE;
            evt.mi.dx = x;
            evt.mi.dy = y;
            evt.mi.dwFlags = dw_flags;
            evt.mi.dwExtraInfo = 0;
            evt.mi.mouseData = mouse_data;
            evt.mi.time = 0;
            const bool ok = WinSendEvent(&evt);
            if (!significant) {
                return;
            }
            if (ok) {
                LOGI("[InputReplay] SendInput ok {} tag={} dwFlags={} mouseData={} abs=({},{})",
                     DescribeButtonFlags(buttons), tag, DescribeDwFlags(dw_flags), mouse_data, x, y);
            } else {
                LOGE("[InputReplay] SendInput FAIL {} tag={} dwFlags={} mouseData={} abs=({},{})",
                     DescribeButtonFlags(buttons), tag, DescribeDwFlags(dw_flags), mouse_data, x, y);
            }
        };

        int target_buttons = 0;
        if (buttons & ButtonFlag::kMouseMove) {
            target_buttons |= MOUSEEVENTF_MOVE;
        }
        if (buttons & ButtonFlag::kLeftMouseButtonDown) {
            target_buttons |= MOUSEEVENTF_LEFTDOWN;
        }
        if (buttons & ButtonFlag::kRightMouseButtonDown) {
            target_buttons |= MOUSEEVENTF_RIGHTDOWN;
        }
        if (buttons & ButtonFlag::kMiddleMouseButtonDown) {
            target_buttons |= MOUSEEVENTF_MIDDLEDOWN;
        }
        if (buttons & ButtonFlag::kLeftMouseButtonUp) {
            target_buttons |= MOUSEEVENTF_LEFTUP;
        }
        if (buttons & ButtonFlag::kRightMouseButtonUp) {
            target_buttons |= MOUSEEVENTF_RIGHTUP;
        }
        if (buttons & ButtonFlag::kMiddleMouseButtonUp) {
            target_buttons |= MOUSEEVENTF_MIDDLEUP;
        }
        if (buttons & ButtonFlag::kMouseEventWheel) {
            target_buttons |= MOUSEEVENTF_WHEEL;
        }
        if (buttons & ButtonFlag::kMouseEventHWheel) {
            target_buttons |= MOUSEEVENTF_HWHEEL;
        }

        target_buttons |= MOUSEEVENTF_ABSOLUTE;
        target_buttons |= MOUSEEVENTF_VIRTUALDESK;

        // 按下:先绝对 MOVE 定位,再 down。相对位移由后续拖动 MOVE 在 server 侧换算。
        const bool is_button_down =
            (target_buttons & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_MIDDLEDOWN)) != 0;
        if (is_button_down) {
            inject_abs(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, 0, "pre-down-move");
            target_buttons &= ~MOUSEEVENTF_MOVE;
            inject_abs(static_cast<DWORD>(target_buttons), data, "down");
            UpdateHeldButtons(buttons);
            return;
        }

        // 移动/抬起等:server 用绝对目标与当前光标差换算相对 MOVE(游戏/UE),再发绝对事件同步光标。
        // client 只提供 ratio,不使用 client delta_*。
        const bool need_rel =
            (buttons & ButtonFlag::kMouseMove) != 0
            || (target_buttons & (MOUSEEVENTF_LEFTUP | MOUSEEVENTF_RIGHTUP | MOUSEEVENTF_MIDDLEUP)) != 0
            || dragging;
        if (need_rel) {
            InjectServerRelFromAbs(x, y, buttons, significant);
        }

        inject_abs(static_cast<DWORD>(target_buttons), data, "evt");
        UpdateHeldButtons(buttons);
    }

    void WinEventReplayer::HandleFocusOutEvent() {

        std::array<int, 11> keys = {
            VK_CONTROL,
            VK_RCONTROL,
            VK_LCONTROL,
            VK_SHIFT,
            VK_RSHIFT,
            VK_LSHIFT,
            VK_MENU,
            VK_RMENU,
            VK_LMENU,
            VK_LWIN,
            VK_RWIN,
        };
        
        for (auto key : keys) {
            tc::KeyEvent event;
            event.set_down(false);
            event.set_key_code(key);
            HandleKeyEvent(event);
        }
    }

    void WinEventReplayer::SimulateCtrlWinShiftB() {
        INPUT inputs[8] = {};
        ZeroMemory(inputs, sizeof(inputs));

        // 按下Ctrl
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_CONTROL;
        inputs[0].ki.dwFlags = 0;

        // 按下Shift
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_SHIFT;
        inputs[1].ki.dwFlags = 0;

        // 按下Win
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = VK_LWIN;
        inputs[2].ki.dwFlags = 0;

        // 按下B
        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wVk = 'B';
        inputs[3].ki.dwFlags = 0;

        // 释放B
        inputs[4].type = INPUT_KEYBOARD;
        inputs[4].ki.wVk = 'B';
        inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;

        // 释放Win
        inputs[5].type = INPUT_KEYBOARD;
        inputs[5].ki.wVk = VK_LWIN;
        inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;

        // 释放Shift
        inputs[6].type = INPUT_KEYBOARD;
        inputs[6].ki.wVk = VK_SHIFT;
        inputs[6].ki.dwFlags = KEYEVENTF_KEYUP;

        // 释放Ctrl
        inputs[7].type = INPUT_KEYBOARD;
        inputs[7].ki.wVk = VK_CONTROL;
        inputs[7].ki.dwFlags = KEYEVENTF_KEYUP;

        // 发送所有输入事件
        SendInput(8, inputs, sizeof(INPUT));
    }
}