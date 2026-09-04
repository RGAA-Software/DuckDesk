//
// Created by RGAA on 2024-02-18.
//

#include "hook_manager.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/file.h"
#include "hk_video/shared_texture.h"
#include <Windows.h>
#include <detours/detours.h>
#include "px_message.pb.h"
#include "app_shared_info_reader.h"
#include "ws_ipc_client.h"

namespace px
{

    void HookManager::Init() {
        auto pid = GetCurrentProcessId();
        current_pid_ = pid;
        // Name kept for boot-file lookup compatibility (file under hook_boot/, not SHM).
        auto boot_name = std::format("application_shm_{}", pid);
        shared_info_reader_ = AppSharedInfoReader::Make(boot_name);
        app_shared_msg_ = shared_info_reader_->ReadData();
        shared_texture_ = std::make_shared<SharedTexture>();
    }

    void HookManager::Send(const std::string& msg) {
        if (msg.empty()) {
            LOGE("HookManager::Send: empty message");
            return;
        }
        if (!ws_ipc_client_) {
            LOGE("HookManager::Send: ws_ipc_client_ null, drop {} bytes", msg.size());
            return;
        }
        ws_ipc_client_->PostIpcMessage(msg);
    }

    void HookManager::PushIpcMessage(const std::shared_ptr<CaptureBaseMessage>& msg) {
        if (messages_.Size() > 512) {
            static uint64_t s_dropped = 0;
            const auto n = ++s_dropped;
            if (n == 1 || (n % 100) == 0) {
                LOGW("PushIpcMessage: queue full, drop oldest n={}", n);
            }
            messages_.PopFront();
        }
        messages_.PushBack(msg);
    }

    void HookManager::ResetInputState() {
        int dropped = 0;
        while (!messages_.Empty()) {
            messages_.PopFront();
            ++dropped;
        }
        last_raw_cursor_valid_ = false;
        raw_input_first_invoke_ = true;
        shift_pressed_.store(false, std::memory_order_relaxed);
        control_pressed_.store(false, std::memory_order_relaxed);
        menu_pressed_.store(false, std::memory_order_relaxed);
        // 新客户端接入：强制下一次输入事件立即重新断言焦点
        last_focus_assert_ms_.store(0, std::memory_order_relaxed);
        LOGI("ResetInputState: dropped {} queued events", dropped);
    }

    void HookManager::AssertGameFocus(HWND hwnd) {
        const int64_t now_ms = static_cast<int64_t>(GetTickCount64());
        const int64_t last_ms = last_focus_assert_ms_.load(std::memory_order_relaxed);
        if (last_ms != 0 && now_ms - last_ms <= 500) {
            return;
        }
        // 参考 streamer 实战经验：
        // - UE4 高频 WM_ACTIVATE 会出按钮卡住/点击失效,故每事件场景用 WM_ACTIVATEAPP;
        // - Unity 失焦恢复需要窗口级 WM_ACTIVATE(streamer 对 Unity 用 100/300ms 定时器发);
        // 这里 500ms 节流三个一起发,兼顾两者,持续对抗 OS 真实焦点变化带来的
        // WM_KILLFOCUS/WM_ACTIVATE(WA_INACTIVE)/WM_ACTIVATEAPP(FALSE)。
        PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        PostMessage(hwnd, WM_ACTIVATEAPP, TRUE, 0);
        PostMessage(hwnd, WM_SETFOCUS, 0, 0);
        last_focus_assert_ms_.store(now_ms, std::memory_order_relaxed);
        static uint64_t s_n = 0;
        const auto n = ++s_n;
        if (n <= 5 || (n % 200) == 0) {
            LOGI("AssertGameFocus: n={} hwnd={:x}", n, reinterpret_cast<uint64_t>(hwnd));
        }
    }

    // ---- FocusGuard: 子类化游戏窗口,吞掉 OS 真实失焦消息 ----
    // WH_CALLWNDPROC 只能观察无法阻止投递,必须替换 WndProc 才能真正吞掉。
    // 目前同时只子类化一个窗口(输入目标窗口),窗口重建时切换。
    static HWND g_focus_guard_hwnd = nullptr;
    static WNDPROC g_focus_guard_origin = nullptr;

    static LRESULT CALLBACK GameFocusGuardWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_KILLFOCUS
            || (msg == WM_ACTIVATE && wp == WA_INACTIVE)
            || (msg == WM_ACTIVATEAPP && wp == FALSE)) {
            // 吞掉:游戏永远不知道自己失焦(远程注入输入依赖游戏自认为有焦点)
            static uint64_t s_n = 0;
            const auto n = ++s_n;
            if (n <= 5 || (n % 100) == 0) {
                LOGI("FocusGuard: swallowed {} n={} hwnd={:x}",
                     msg == WM_KILLFOCUS ? "WM_KILLFOCUS" : (msg == WM_ACTIVATE ? "WM_ACTIVATE(INACTIVE)" : "WM_ACTIVATEAPP(FALSE)"),
                     n, reinterpret_cast<uint64_t>(hwnd));
            }
            return 0;
        }
        return CallWindowProc(g_focus_guard_origin, hwnd, msg, wp, lp);
    }

    void HookManager::EnsureGameWindowSubclassed(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) {
            return;
        }
        if (g_focus_guard_hwnd == hwnd) {
            return;
        }
        auto origin = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GameFocusGuardWndProc)));
        if (origin) {
            g_focus_guard_origin = origin;
            g_focus_guard_hwnd = hwnd;
            LOGI("FocusGuard: subclassed game hwnd={:x}", reinterpret_cast<uint64_t>(hwnd));
        } else {
            LOGW("FocusGuard: subclass failed hwnd={:x} err={}",
                 reinterpret_cast<uint64_t>(hwnd), GetLastError());
        }
    }

    UINT WINAPI HookedGetRawInputData(
            HRAWINPUT hRawInput,
            UINT uiCommand,
            LPVOID pData,
            PUINT pcbSize,
            UINT cbSizeHeader) {
        auto hm = HookManager::Instance();
        if (hm->app_shared_msg_ && hm->app_shared_msg_->enable_hook_events_ == 0) {
            return origin_GetRawInputData_(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
        }
        return HookManager::Instance()->ProcessHookedGetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    }

    UINT WINAPI HookedGetRawInputBuffer(
            PRAWINPUT pData,
            PUINT pcbSize,
            UINT cbSizeHeader) {
        return origin_GetRawInputBuffer_(pData, pcbSize, cbSizeHeader);
    }

    BOOL WINAPI HookedGetCursorPos(LPPOINT lpPoint) {
        auto hm = HookManager::Instance();
        if (hm->app_shared_msg_ && hm->app_shared_msg_->enable_hook_events_ == 0) {
            return origin_GetCursorPos_(lpPoint);
        }
        return HookManager::Instance()->ProcessHookedGetCursorPos(lpPoint);
    }

    SHORT HookedGetAsyncKeyState(int vKey) {
        auto hm = HookManager::Instance();
        if (hm->app_shared_msg_ && hm->app_shared_msg_->enable_hook_events_ != 0) {
            return hm->ProcessHookedGetAsyncKeyState(vKey);
        }
        return origin_GetAsyncKeyState_ ? origin_GetAsyncKeyState_(vKey) : 0;
    }

    SHORT HookedGetKeyState(int vKey) {
        auto hm = HookManager::Instance();
        if (hm->app_shared_msg_ && hm->app_shared_msg_->enable_hook_events_ != 0) {
            return hm->ProcessHookedGetKeyState(vKey);
        }
        return origin_GetKeyState_ ? origin_GetKeyState_(vKey) : 0;
    }

    HRESULT HookedDirectInput8Create(
            HINSTANCE hinst,
            DWORD dwVersion,
            REFIID riidltf,
            LPVOID * ppvOut,
            LPUNKNOWN punkOuter) {
        LOGI("HookedDirectInput8Create");
        return origin_DirectInput8Create_(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    }

    BOOL WINAPI HookedIsWindowVisible(HWND hWnd) {
        auto hm = HookManager::Instance();
        // Keep own game window visible to the process (background / multi-open).
        if (hm->WantFocusSpoof()) {
            const HWND spoof = hm->FocusSpoofHwnd();
            if (spoof && (hWnd == spoof || !hWnd)) {
                return TRUE;
            }
        }
        return origin_IsWindowVisibleHooked_(hWnd);
    }

    HWND WINAPI HookedGetForegroundWindow(VOID) {
        auto hm = HookManager::Instance();
        if (hm->WantFocusSpoof()) {
            if (HWND spoof = hm->FocusSpoofHwnd()) {
                return spoof;
            }
        }
        return origin_GetForegroundWindowHooked_();
    }

    HWND WINAPI HookedGetActiveWindow(VOID) {
        auto hm = HookManager::Instance();
        if (hm->WantFocusSpoof()) {
            if (HWND spoof = hm->FocusSpoofHwnd()) {
                return spoof;
            }
        }
        return origin_GetActiveWindow_ ? origin_GetActiveWindow_() : nullptr;
    }

    HWND WINAPI HookedGetFocus(VOID) {
        auto hm = HookManager::Instance();
        if (hm->WantFocusSpoof()) {
            if (HWND spoof = hm->FocusSpoofHwnd()) {
                return spoof;
            }
        }
        return origin_GetFocus_ ? origin_GetFocus_() : nullptr;
    }

    HWND HookedWindowFromPoint(_In_ POINT Point) {
        auto hm = HookManager::Instance();
        // Only rewrite hit-testing when streaming input events are enabled.
        if (hm->app_shared_msg_ && hm->app_shared_msg_->enable_hook_events_ &&
            hm->FocusSpoofHwnd()) {
            return hm->ProcessWindowFromPoint(Point);
        }
        return origin_WindowFromPoint_(Point);
    }

    BOOL HookedClipCursor(_In_opt_ CONST RECT *lpRect) {
        auto hm = HookManager::Instance();
        if (!hm->app_shared_msg_ || hm->app_shared_msg_->enable_hook_events_ == 0) {
            return origin_ClipCursor_(lpRect);
        }
        return TRUE;
    }

    BOOL WINAPI HookedSetCursorPos(int x, int y) {
        auto hm = HookManager::Instance();
        if (!hm->app_shared_msg_ || hm->app_shared_msg_->enable_hook_events_ == 0) {
            return origin_SetCursorPos_ ? origin_SetCursorPos_(x, y) : TRUE;
        }
        hm->OnGameSetCursorPos(x, y);
        return TRUE;
    }

    int WINAPI HookedGetSystemMetrics(int nIndex) {
        auto hm = HookManager::Instance();
        if (nIndex == SM_REMOTESESSION && hm->app_shared_msg_
            && (hm->app_shared_msg_->enable_hook_events_ != 0
                || hm->app_shared_msg_->enable_hook_audio_ != 0)) {
            // 伪装成本地会话：部分游戏/引擎在远程会话(SM_REMOTESESSION=1)下会禁用功能
            return 0;
        }
        return origin_GetSystemMetrics_ ? origin_GetSystemMetrics_(nIndex) : 0;
    }

    void HookManager::OnGameSetCursorPos(int x, int y) {
        // 游戏主动居中/锁定光标（UE/Unity 视角模式常见）：
        // 吞掉对物理光标的修改，只同步内部伪造光标，
        // 这样游戏随后读 GetCursorPos 拿到的就是它刚设置的值
        cursor_in_screen_position_.x = x;
        cursor_in_screen_position_.y = y;
    }

    void HookManager::HookMethods() {
        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        // GetRawInputBuffer
        {
            origin_GetRawInputBuffer_ = (GetRawInputBuffer_t) GetProcAddress(GetModuleHandle(TEXT("User32")), "GetRawInputBuffer");
            auto r = DetourAttach(&(PVOID &)origin_GetRawInputBuffer_, &(PVOID &)HookedGetRawInputBuffer);
            LOGI("Hook GetRawInputBuffer result: {}", r);
        }
        // GetRawInputData
        {
            origin_GetRawInputData_ = GetProcAddressByName<GetRawInputData_t>(L"User32", "GetRawInputData");
            auto r = DetourAttach(&(PVOID &)origin_GetRawInputData_, &(PVOID &)HookedGetRawInputData);
            LOGI("Hook GetRawInputData result: {}", r);
        }
        // GetCursorPos
        {
            origin_GetCursorPos_ = GetProcAddressByName<GetCursorPos_t>(L"User32", "GetCursorPos");
            auto r = DetourAttach(&(PVOID &)origin_GetCursorPos_, &(PVOID &)HookedGetCursorPos);
            LOGI("Hook GetCursorPos result: {}", r);
        }
        // GetAsyncKeyState
        if (true) {
            origin_GetAsyncKeyState_ = GetProcAddressByName<GetAsyncKeyState_t>(L"User32", "GetAsyncKeyState");
            auto r = DetourAttach(&(PVOID &)origin_GetAsyncKeyState_, &(PVOID &)HookedGetAsyncKeyState);
            LOGI("Hook GetAsyncKeyState result: {}", r);
        }
        // GetKeyState
        if (true) {
            origin_GetKeyState_ = GetProcAddressByName<GetKeyState_t>(L"User32", "GetKeyState");
            auto r = DetourAttach(&(PVOID &)origin_GetKeyState_, &(PVOID &) HookedGetKeyState);
            LOGI("Hook GetKeyState result: {}", r);
        }
        //DirectInput8Create
        if (false) {
            origin_DirectInput8Create_ = GetProcAddressByName<DirectInput8Create_t>(L"Dinput8", "DirectInput8Create");
            auto r = DetourAttach(&(PVOID &)origin_DirectInput8Create_, &(PVOID &) HookedDirectInput8Create);
            LOGI("Hook DirectInput8Create result: {}", r);
        }
        //IsWindowVisible
        {
            origin_IsWindowVisibleHooked_ = GetProcAddressByName<IsWindowVisibleHooked_t>(L"User32", "IsWindowVisible");
            auto r = DetourAttach(&(PVOID &)origin_IsWindowVisibleHooked_, &(PVOID &) HookedIsWindowVisible);
            LOGI("Hook IsWindowVisible result: {}", r);
        }
        //GetForegroundWindow / GetActiveWindow / GetFocus — keep audio alive
        // when the OS focus is on another instance or desktop window.
        {
            origin_GetForegroundWindowHooked_ = GetProcAddressByName<GetForegroundWindowHooked_t>(L"User32", "GetForegroundWindow");
            auto r = DetourAttach(&(PVOID &)origin_GetForegroundWindowHooked_, &(PVOID &) HookedGetForegroundWindow);
            LOGI("Hook GetForegroundWindow result: {}", r);
        }
        {
            origin_GetActiveWindow_ = GetProcAddressByName<GetActiveWindow_t>(L"User32", "GetActiveWindow");
            auto r = DetourAttach(&(PVOID &)origin_GetActiveWindow_, &(PVOID &) HookedGetActiveWindow);
            LOGI("Hook GetActiveWindow result: {}", r);
        }
        {
            origin_GetFocus_ = GetProcAddressByName<GetFocus_t>(L"User32", "GetFocus");
            auto r = DetourAttach(&(PVOID &)origin_GetFocus_, &(PVOID &) HookedGetFocus);
            LOGI("Hook GetFocus result: {}", r);
        }
        //
        {
            origin_WindowFromPoint_ = GetProcAddressByName<WindowFromPoint_t>(L"User32", "WindowFromPoint");
            auto r = DetourAttach(&(PVOID &)origin_WindowFromPoint_, &(PVOID &) HookedWindowFromPoint);
            LOGI("Hook WindowFromPoint result: {}", r);
        }
        //
        {
            origin_ClipCursor_ = GetProcAddressByName<ClipCursor_t>(L"User32", "ClipCursor");
            auto r = DetourAttach(&(PVOID &)origin_ClipCursor_, &(PVOID &) HookedClipCursor);
            LOGI("Hook ClipCursor result: {}", r);
        }
        // SetCursorPos
        {
            origin_SetCursorPos_ = GetProcAddressByName<SetCursorPos_t>(L"User32", "SetCursorPos");
            auto r = DetourAttach(&(PVOID &)origin_SetCursorPos_, &(PVOID &) HookedSetCursorPos);
            LOGI("Hook SetCursorPos result: {}", r);
        }
        // GetSystemMetrics (SM_REMOTESESSION spoof)
        {
            origin_GetSystemMetrics_ = GetProcAddressByName<GetSystemMetrics_t>(L"User32", "GetSystemMetrics");
            auto r = DetourAttach(&(PVOID &)origin_GetSystemMetrics_, &(PVOID &) HookedGetSystemMetrics);
            LOGI("Hook GetSystemMetrics result: {}", r);
        }
        DetourTransactionCommit();
    }

    bool HookManager::WantFocusSpoof() const {
        if (!app_shared_msg_) {
            return false;
        }
        // Audio hook and/or input events: both need the game to think it is focused
        // so multi-open background instances keep playing sound.
        return app_shared_msg_->enable_hook_audio_ != 0 ||
               app_shared_msg_->enable_hook_events_ != 0;
    }

    HWND HookManager::FocusSpoofHwnd() const {
        if (hwnd_ && IsWindow(hwnd_)) {
            return hwnd_;
        }
        if (own_game_hwnd_ && IsWindow(own_game_hwnd_)) {
            return own_game_hwnd_;
        }
        return nullptr;
    }

    void HookManager::RefreshOwnGameHwnd() {
        struct Ctx {
            DWORD pid = 0;
            HWND best = nullptr;
            LONG best_area = 0;
        } ctx;
        ctx.pid = current_pid_ ? current_pid_ : GetCurrentProcessId();
        EnumWindows(
            [](HWND w, LPARAM lp) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(lp);
                DWORD wpid = 0;
                GetWindowThreadProcessId(w, &wpid);
                if (wpid != c->pid) {
                    return TRUE;
                }
                // Do not call IsWindowVisible here — it may already be hooked.
                if (GetWindow(w, GW_OWNER) != nullptr) {
                    return TRUE;
                }
                if ((GetWindowLongW(w, GWL_STYLE) & WS_VISIBLE) == 0) {
                    return TRUE;
                }
                RECT rc{};
                if (!GetClientRect(w, &rc)) {
                    return TRUE;
                }
                const LONG area = (rc.right - rc.left) * (rc.bottom - rc.top);
                if (area > c->best_area) {
                    c->best_area = area;
                    c->best = w;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&ctx));
        if (ctx.best && ctx.best != own_game_hwnd_) {
            own_game_hwnd_ = ctx.best;
            LOGI("FocusSpoof: own game hwnd={:x} area={}", reinterpret_cast<uint64_t>(ctx.best),
                 ctx.best_area);
        }
    }

    void HookManager::FocusSpoofWatcherMain() {
        for (int i = 0; i < 1200 && !focus_spoof_stop_; i++) {
            RefreshOwnGameHwnd();
            if (own_game_hwnd_) {
                // Keep refreshing occasionally in case the main window is recreated.
                Sleep(1000);
            } else {
                Sleep(50);
            }
        }
    }

    void HookManager::StartFocusSpoof() {
        if (!WantFocusSpoof()) {
            LOGI("FocusSpoof: not needed (audio/events both off)");
            return;
        }
        RefreshOwnGameHwnd();
        HookMethods();
        if (!focus_spoof_watcher_) {
            focus_spoof_stop_ = 0;
            focus_spoof_watcher_ = CreateThread(
                nullptr, 0,
                [](LPVOID p) -> DWORD {
                    reinterpret_cast<HookManager*>(p)->FocusSpoofWatcherMain();
                    return 0;
                },
                this, 0, nullptr);
        }
        LOGI("FocusSpoof: started (GetForegroundWindow/GetActiveWindow/GetFocus → own hwnd)");
    }

    UINT HookManager::ProcessHookedGetRawInputData(
            HRAWINPUT hRawInput,
            UINT uiCommand,
            LPVOID pData,
            PUINT pcbSize,
            UINT cbSizeHeader) {

        if (uiCommand != RID_INPUT || hRawInput) {
            return origin_GetRawInputData_(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
        }
        if (!pData) {
            if (!pcbSize) {
                return 0;
            }
            *pcbSize = sizeof(RAWINPUT);
            return 0;
        }
        if (!pcbSize || *pcbSize < sizeof(RAWINPUT)) {
            return static_cast<UINT>(-1);
        }

        if (messages_.Empty()) {
            return 0;
        }

        auto msg = messages_.Front();
        messages_.PopFront();
        // First consume: drop stale queued events (Unity/UE restart can leave a backlog).
        if (raw_input_first_invoke_) {
            int dropped = 0;
            while (!messages_.Empty()) {
                auto next = messages_.Front();
                messages_.PopFront();
                if (next.has_value() && next.value()) {
                    msg = next;
                }
                ++dropped;
            }
            if (dropped > 0) {
                LOGI("GetRawInputData first invoke: dropped {} stale events", dropped);
            }
            raw_input_first_invoke_ = false;
        }
        if (!msg.has_value() || !msg.value()) {
            return 0;
        }

        auto raw_input = (RAWINPUT*)pData;
        memset(raw_input, 0, sizeof(RAWINPUT));
        if (msg.value()->type_ == kKeyboardEventMessage) {
            auto keyboard_msg = std::static_pointer_cast<KeyboardEventMessage>(msg.value());
            bool down = keyboard_msg->down_;
            int k = static_cast<int>(keyboard_msg->key_);
            raw_input->header.dwType = RIM_TYPEKEYBOARD;
            raw_input->data.keyboard.VKey = k;
            raw_input->data.keyboard.MakeCode = MapVirtualKey(k, MAPVK_VK_TO_VSC);
            raw_input->data.keyboard.Flags = down ? RI_KEY_MAKE : RI_KEY_BREAK;
            raw_input->data.keyboard.Message = down ? WM_KEYDOWN : WM_KEYUP;
            return sizeof(RAWINPUT);
        }
        if (msg.value()->type_ == kMouseEventMessage) {
            auto mouse_msg = std::static_pointer_cast<MouseEventMessage>(msg.value());

            // 游戏消费慢于事件到达时，把队列里连续的纯移动事件合并成最新一条，
            // 否则积压会导致操作延迟/拖动卡顿（相对位移总量不变，中间位置可丢）
            auto is_pure_move = [](const std::shared_ptr<CaptureBaseMessage>& m) {
                if (!m || m->type_ != kMouseEventMessage) {
                    return false;
                }
                auto mm = std::static_pointer_cast<MouseEventMessage>(m);
                return !mm->pressed_ && !mm->released_ && mm->data_ == 0;
            };
            int merged = 0;
            if (is_pure_move(msg.value())) {
                while (!messages_.Empty()) {
                    auto next = messages_.Front();
                    if (!next.has_value() || !is_pure_move(next.value())) {
                        break;
                    }
                    messages_.PopFront();
                    mouse_msg = std::static_pointer_cast<MouseEventMessage>(next.value());
                    ++merged;
                }
            }

            raw_input->header.dwType = RIM_TYPEMOUSE;
            // 客户端只发比例，render 已换算成屏幕绝对坐标；
            // 这里用相邻两次绝对坐标差分出相对位移（RawInput 游戏读相对位移）
            const LONG cur_x = static_cast<LONG>(mouse_msg->x_);
            const LONG cur_y = static_cast<LONG>(mouse_msg->y_);
            LONG dx = 0, dy = 0;
            if (last_raw_cursor_valid_) {
                dx = cur_x - last_raw_cursor_.x;
                dy = cur_y - last_raw_cursor_.y;
            }
            last_raw_cursor_.x = cur_x;
            last_raw_cursor_.y = cur_y;
            last_raw_cursor_valid_ = true;
            {
                static thread_local uint64_t s_n = 0;
                const auto n = ++s_n;
                if (n <= 5 || (n % 200) == 0) {
                    LOGI("RawInput mouse: n={} abs=({},{}) delta=({},{}) merged={}",
                         n, cur_x, cur_y, dx, dy, merged);
                }
            }
            raw_input->data.mouse.lLastX = dx;
            raw_input->data.mouse.lLastY = dy;
            raw_input->data.mouse.usFlags = MOUSE_MOVE_RELATIVE;

            if (mouse_msg->data_) {
                raw_input->data.mouse.ulButtons |= RI_MOUSE_WHEEL;
                raw_input->data.mouse.usButtonData = static_cast<USHORT>(mouse_msg->data_);
            }

            if (mouse_msg->pressed_) {
                if (mouse_msg->button_ == ButtonFlag::kLeftMouseButtonDown) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_LEFT_BUTTON_DOWN;
                } else if (mouse_msg->button_ == ButtonFlag::kMiddleMouseButtonDown) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_MIDDLE_BUTTON_DOWN;
                } else if (mouse_msg->button_ == ButtonFlag::kRightMouseButtonDown) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_RIGHT_BUTTON_DOWN;
                }
            } else if (mouse_msg->released_) {
                if (mouse_msg->button_ == ButtonFlag::kLeftMouseButtonUp) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_LEFT_BUTTON_UP;
                } else if (mouse_msg->button_ == ButtonFlag::kMiddleMouseButtonUp) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_MIDDLE_BUTTON_UP;
                } else if (mouse_msg->button_ == ButtonFlag::kRightMouseButtonUp) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_RIGHT_BUTTON_UP;
                }
            }
            return sizeof(RAWINPUT);
        }
        return origin_GetRawInputData_(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    }

    BOOL HookManager::ProcessHookedGetCursorPos(LPPOINT lpPoint) {
        if (!lpPoint) {
            return false;
        }
        BOOL ret = false;
        // todo没人连接时，使用原本的坐标获取方法
        //auto ret = origin_GetCursorPos(lpPoint);
        if (lpPoint) {
            lpPoint->x = cursor_in_screen_position_.x;
            lpPoint->y = cursor_in_screen_position_.y;
            ret = true;
        }
        //LOGI("GetCursorPos : %d, %d", lpPoint->x, lpPoint->y);
        return ret;
    }

    HWND HookManager::ProcessHookedGetForegroundWindow() const {
        return FocusSpoofHwnd();
    }

    HWND HookManager::ProcessWindowFromPoint(_In_ POINT Point) const {
        if (hwnd_) {
            RECT rect;
            if (GetWindowRect(hwnd_, &rect)) {
                if (Point.x >= rect.left && Point.x <= rect.right && Point.y >= rect.top && Point.y <= rect.bottom) {
                    return hwnd_;
                }
            }
        }
        return origin_WindowFromPoint_(Point);
    }

    HWND HookManager::ResolveInputHwnd(uint64_t hwnd_from_msg) const {
        auto hwnd = reinterpret_cast<HWND>(hwnd_from_msg);
        if (hwnd && IsWindow(hwnd)) {
            return hwnd;
        }
        return FocusSpoofHwnd();
    }

    void HookManager::UpdateModifierState(uint32_t key, bool down, uint32_t caps_lock_state) {
        if (key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT) {
            shift_pressed_.store(down, std::memory_order_relaxed);
        }
        if (key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL) {
            control_pressed_.store(down, std::memory_order_relaxed);
        }
        if (key == VK_MENU || key == VK_LMENU || key == VK_RMENU) {
            menu_pressed_.store(down, std::memory_order_relaxed);
        }
        if (key >= 'A' && key <= 'Z') {
            caps_lock_status_.store(static_cast<SHORT>(caps_lock_state), std::memory_order_relaxed);
        }
        if (key == VK_CAPITAL) {
            caps_lock_status_.store(static_cast<SHORT>(caps_lock_state), std::memory_order_relaxed);
        }
    }

    SHORT HookManager::ProcessHookedGetKeyState(int vKey) const {
        switch (vKey) {
            case VK_CAPITAL:
                return caps_lock_status_.load(std::memory_order_relaxed);
            case VK_CONTROL:
            case VK_LCONTROL:
            case VK_RCONTROL:
                return control_pressed_.load(std::memory_order_relaxed) ? static_cast<SHORT>(0x8000) : 0;
            case VK_LSHIFT:
            case VK_RSHIFT:
            case VK_SHIFT:
                return shift_pressed_.load(std::memory_order_relaxed) ? static_cast<SHORT>(0x8000) : 0;
            case VK_MENU:
            case VK_LMENU:
            case VK_RMENU:
                return menu_pressed_.load(std::memory_order_relaxed) ? static_cast<SHORT>(0x8000) : 0;
            default:
                break;
        }
        return origin_GetKeyState_ ? origin_GetKeyState_(vKey) : 0;
    }

    SHORT HookManager::ProcessHookedGetAsyncKeyState(int vKey) const {
        switch (vKey) {
            case VK_CAPITAL:
                return caps_lock_status_.load(std::memory_order_relaxed);
            case VK_CONTROL:
            case VK_LCONTROL:
            case VK_RCONTROL:
                return control_pressed_.load(std::memory_order_relaxed) ? static_cast<SHORT>(0x8000) : 0;
            case VK_LSHIFT:
            case VK_RSHIFT:
            case VK_SHIFT:
                return shift_pressed_.load(std::memory_order_relaxed) ? static_cast<SHORT>(0x8000) : 0;
            case VK_MENU:
            case VK_LMENU:
            case VK_RMENU:
                return menu_pressed_.load(std::memory_order_relaxed) ? static_cast<SHORT>(0x8000) : 0;
            default:
                break;
        }
        return origin_GetAsyncKeyState_ ? origin_GetAsyncKeyState_(vKey) : 0;
    }

    void HookManager::GenerateMouseEvent(const std::shared_ptr<CaptureBaseMessage>& msg) {
        auto message = std::static_pointer_cast<MouseEventMessage>(msg);
        HWND hwnd = ResolveInputHwnd(message->hwnd_);
        if (!hwnd) {
            static thread_local uint64_t s_n = 0;
            if (++s_n == 1 || (s_n % 100) == 0) {
                LOGW("GenerateMouseEvent: no hwnd, drop n={}", s_n);
            }
            return;
        }
        hwnd_ = hwnd;
        cursor_in_screen_position_.x = static_cast<LONG>(message->x_);
        cursor_in_screen_position_.y = static_cast<LONG>(message->y_);

        {
            static thread_local uint64_t s_ok = 0;
            const auto n = ++s_ok;
            if (n <= 5 || (n % 200) == 0) {
                LOGI("GenerateMouseEvent: n={} hwnd={:x} xy=({},{}) btn={} pressed={} released={}",
                     n, reinterpret_cast<uint64_t>(hwnd), message->x_, message->y_,
                     message->button_, message->pressed_, message->released_);
            }
        }

        POINT client_area_point = {
            .x = static_cast<LONG>(message->x_),
            .y = static_cast<LONG>(message->y_),
        };
        ScreenToClient(hwnd, &client_area_point);

        EnsureGameWindowSubclassed(hwnd);
        AssertGameFocus(hwnd);

        DWORD mouse_key_state_flags = 0;
        UINT event = WM_MOUSEMOVE;
        if (message->pressed_) {
            if (message->button_ == ButtonFlag::kLeftMouseButtonDown) {
                event = WM_LBUTTONDOWN;
                mouse_key_state_flags = MK_LBUTTON;
            } else if (message->button_ == ButtonFlag::kRightMouseButtonDown) {
                event = WM_RBUTTONDOWN;
                mouse_key_state_flags = MK_RBUTTON;
            } else if (message->button_ == ButtonFlag::kMiddleMouseButtonDown) {
                event = WM_MBUTTONDOWN;
                mouse_key_state_flags = MK_MBUTTON;
            }
        } else if (message->released_) {
            if (message->button_ == ButtonFlag::kLeftMouseButtonUp) {
                event = WM_LBUTTONUP;
                mouse_key_state_flags = MK_LBUTTON;
            } else if (message->button_ == ButtonFlag::kRightMouseButtonUp) {
                event = WM_RBUTTONUP;
                mouse_key_state_flags = MK_RBUTTON;
            } else if (message->button_ == ButtonFlag::kMiddleMouseButtonUp) {
                event = WM_MBUTTONUP;
                mouse_key_state_flags = MK_MBUTTON;
            }
        }

        if (message->data_) {
            PostMessage(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, message->data_),
                        MAKELPARAM(client_area_point.x, client_area_point.y));
        }

        PostMessage(hwnd, WM_MOUSEMOVE, mouse_key_state_flags,
                    MAKELPARAM(client_area_point.x, client_area_point.y));
        if (event != WM_MOUSEMOVE) {
            PostMessage(hwnd, event, mouse_key_state_flags,
                        MAKELPARAM(client_area_point.x, client_area_point.y));
        }
        PostMessage(hwnd, WM_INPUT, 0, (LPARAM)nullptr);
    }

    void HookManager::GenerateKeyboardEvent(const std::shared_ptr<CaptureBaseMessage>& m) {
        auto key_msg = std::static_pointer_cast<KeyboardEventMessage>(m);
        HWND hwnd = ResolveInputHwnd(key_msg->hwnd_);
        if (!hwnd) {
            static thread_local uint64_t s_n = 0;
            if (++s_n == 1 || (s_n % 100) == 0) {
                LOGW("GenerateKeyboardEvent: no hwnd, drop n={}", s_n);
            }
            return;
        }

        EnsureGameWindowSubclassed(hwnd);
        AssertGameFocus(hwnd);

        const uint32_t k = key_msg->key_;
        const bool down = key_msg->down_ != 0;
        {
            static thread_local uint64_t s_ok = 0;
            const auto n = ++s_ok;
            if (n <= 5 || (n % 200) == 0) {
                LOGI("GenerateKeyboardEvent: n={} hwnd={:x} key=0x{:x} down={}",
                     n, reinterpret_cast<uint64_t>(hwnd), k, down);
            }
        }
        UpdateModifierState(k, down, key_msg->caps_lock_state_);

        int msg = down ? WM_KEYDOWN : WM_KEYUP;
        LPARAM lp = 0;
        if (!down) {
            lp = 1;            // repeat count
            lp |= 1 << 30;     // previous key state
            lp |= 1 << 31;     // transition state
        }

        UINT vsc = 0;
        if (k == VK_SHIFT) {
            vsc = MapVirtualKey(VK_LSHIFT, MAPVK_VK_TO_VSC_EX);
        } else {
            vsc = MapVirtualKey(k, MAPVK_VK_TO_VSC_EX);
        }
        lp |= static_cast<LPARAM>(vsc) << 16;

        static constexpr uint32_t kExtendedKeys[] = {
            VK_DELETE, VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN, VK_NUMLOCK,
            VK_RCONTROL, VK_RMENU, VK_DIVIDE, VK_LWIN, VK_RWIN,
            VK_HOME, VK_PRIOR, VK_NEXT, VK_END, VK_INSERT,
        };
        for (uint32_t ek : kExtendedKeys) {
            if (ek == k) {
                lp |= 1 << 24;
                break;
            }
        }

        // UE often needs KEYUP twice for some keys (streamer parity).
        const int exec_count = down ? 1 : 2;
        for (int i = 0; i < exec_count; ++i) {
            PostMessage(hwnd, msg, k, lp);
        }
        // Unity/UE RawInput: caller already PushIpcMessage'd once; duplicate + WM_INPUT×2
        // (streamer HandleKeyEvent) so GetRawInputData sees the key.
        PushIpcMessage(m);
        PostMessage(hwnd, WM_INPUT, 0, (LPARAM)nullptr);
        PostMessage(hwnd, WM_INPUT, 0, (LPARAM)nullptr);
    }

    void HookManager::DumpSharedMessage() {
        LOGI("----Begin AppSharedMessage----");
        LOGI("ipc port: {}", app_shared_msg_->ipc_port_);
        LOGI("msg size: {}", app_shared_msg_->self_size_);
        LOGI("enable_hook_events: {}", app_shared_msg_->enable_hook_events_);
        LOGI("enable_hook_audio: {}", app_shared_msg_->enable_hook_audio_);
        LOGI("Hello msg : present:{:x}, present1: {:x}, resize: {:x}, release: {:x}",
             app_shared_msg_->dxgi_present, app_shared_msg_->dxgi_present1, app_shared_msg_->dxgi_resize, app_shared_msg_->dxgi_release);
        LOGI("----End AppSharedMessage----");
    }

    void HookManager::StartIpcClient() {
        ws_ipc_client_ = WsIpcClient::Make((int)app_shared_msg_->ipc_port_);
        ws_ipc_client_->Start();
        const std::weak_ptr<HookManager> weak_manager = Instance();
        ws_ipc_client_->RegisterIpcMessageCallback([weak_manager](const std::shared_ptr<CaptureBaseMessage>& msg) {
            const auto manager = weak_manager.lock();
            if (!manager) {
                return;
            }
            if (msg->type_ == kCaptureResetInputMessage) {
                manager->ResetInputState();
                return;
            }
            manager->PushIpcMessage(msg);
            if (msg->type_ == kMouseEventMessage) {
                manager->GenerateMouseEvent(msg);
            }
            else if (msg->type_ == kKeyboardEventMessage) {
                manager->GenerateKeyboardEvent(msg);
            }
        });
    }
}
