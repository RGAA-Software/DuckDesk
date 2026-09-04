//
// Created by RGAA on 2024-02-18.
//

#ifndef TC_APPLICATION_HOOK_MANAGER_H
#define TC_APPLICATION_HOOK_MANAGER_H

#include <string>
#include <memory>
#include <functional>
#include <queue>
#include <atomic>
#include "px_capture_new/capture_message.h"
#include "px_common_new/concurrent_queue.h"
#include "hook_api.h"
#include <Windows.h>

namespace px
{

    class Data;
    class SharedTexture;
    class AppSharedInfoReader;
    class AppSharedMessage;
    class WsIpcClient;

    class HookManager {
    public:

        static std::shared_ptr<HookManager> Instance() {
            static const auto instance = std::make_shared<HookManager>();
            return instance;
        }

        void Init();
        void Send(const std::string& msg);
        inline uint64_t AppendFrameIndex() { return frame_index_++; }
        void PushIpcMessage(const std::shared_ptr<CaptureBaseMessage>& msg);

        template<typename T>
        T GetProcAddressByName(const std::wstring& dll_name, const std::string& method_name) {
            auto m = (T) GetProcAddress(GetModuleHandleW(dll_name.c_str()), method_name.c_str());
            return m;
        }
        void HookMethods();
        // Focus spoof for background / multi-instance audio: make this process
        // believe its own game window is foreground even when OS focus is elsewhere.
        void StartFocusSpoof();

        UINT ProcessHookedGetRawInputData(
                HRAWINPUT hRawInput,
                UINT uiCommand,
                LPVOID pData,
                PUINT pcbSize,
                UINT cbSizeHeader);
        BOOL ProcessHookedGetCursorPos(LPPOINT lpPoint);
        // 游戏主动 SetCursorPos（视角居中/锁定）时同步内部伪造光标
        void OnGameSetCursorPos(int x, int y);

        // Virtual modifier / caps state for background hook-inner input (streamer parity).
        SHORT ProcessHookedGetKeyState(int vKey) const;
        SHORT ProcessHookedGetAsyncKeyState(int vKey) const;

        [[nodiscard]]
        HWND ProcessHookedGetForegroundWindow() const;

        HWND ProcessWindowFromPoint(_In_ POINT Point) const;

        // Preferred spoof target: mouse IPC hwnd, else auto-discovered own window.
        [[nodiscard]]
        HWND FocusSpoofHwnd() const;
        bool WantFocusSpoof() const;
        void RefreshOwnGameHwnd();

        void DumpSharedMessage();

        void StartIpcClient();

    private:
        void GenerateMouseEvent(const std::shared_ptr<CaptureBaseMessage>& msg);
        void GenerateKeyboardEvent(const std::shared_ptr<CaptureBaseMessage>& msg);
        // 客户端新连接：清积压事件 + 重置差分基准/修饰键状态
        void ResetInputState();

        // 向游戏窗口断言焦点（节流 500ms）。游戏窗口可能因真实焦点变化、
        // 全屏/窗口化切换收到 OS 发来的 WM_KILLFOCUS，之后 Unity/UE 会忽略
        // 注入的输入；只在首次事件断言一次不可靠，需要在事件流中持续重新断言。
        void AssertGameFocus(HWND hwnd);
        // 兜底：子类化游戏窗口 WndProc，直接吞掉 OS 发来的失焦消息
        // (WM_KILLFOCUS / WM_ACTIVATE(WA_INACTIVE) / WM_ACTIVATEAPP(FALSE)),
        // 让游戏永远不知道自己失焦。窗口重建(hwnd 变化)时自动重新子类化。
        void EnsureGameWindowSubclassed(HWND hwnd);
        void FocusSpoofWatcherMain();
        void UpdateModifierState(uint32_t key, bool down, uint32_t caps_lock_state);
        [[nodiscard]] HWND ResolveInputHwnd(uint64_t hwnd_from_msg) const;

    public:
        uint32_t current_pid_{};
        std::wstring dll_path_;
        std::shared_ptr<SharedTexture> shared_texture_ = nullptr;
        uint64_t frame_index_ = 0;

        px::ConcurrentQueue<std::shared_ptr<CaptureBaseMessage>> messages_;
        POINT cursor_in_screen_position_{};

        std::shared_ptr<AppSharedInfoReader> shared_info_reader_ = nullptr;
        std::shared_ptr<AppSharedMessage> app_shared_msg_ = nullptr;

        std::shared_ptr<WsIpcClient> ws_ipc_client_ = nullptr;

        // Set by mouse IPC from host (streaming input target).
        HWND hwnd_ = nullptr;
        // Auto-discovered top-level window of this process (multi-open audio).
        HWND own_game_hwnd_ = nullptr;
        HANDLE focus_spoof_watcher_ = nullptr;
        volatile long focus_spoof_stop_ = 0;

        std::atomic_bool shift_pressed_{false};
        std::atomic_bool control_pressed_{false};
        std::atomic_bool menu_pressed_{false};
        std::atomic<SHORT> caps_lock_status_{0};
        bool raw_input_first_invoke_{true};

        // RawInput 合成：上一次的绝对坐标，用于差分出相对位移
        // （客户端只发比例，render 换算成屏幕绝对坐标，这里相邻两次差分）
        POINT last_raw_cursor_{};
        bool last_raw_cursor_valid_{false};

        // 最近一次向游戏窗口断言焦点(WM_ACTIVATEAPP/WM_SETFOCUS)的时间(GetTickCount64 ms)。
        // 0 表示需要立即重新断言（首次事件 / ResetInputState 后）。
        std::atomic<int64_t> last_focus_assert_ms_{0};
    };

    static GetRawInputBuffer_t origin_GetRawInputBuffer_;
    static GetRawInputData_t origin_GetRawInputData_;
    static PostMessageA_t origin_PostMessageA_;
    static PostMessageW_t origin_PostMessageW_;
    static SendMessageA_t origin_SendMessageA_;
    static SendMessageW_t origin_SendMessageW_;
    static GetCursorPos_t origin_GetCursorPos_;
    static SetCursorPos_t origin_SetCursorPos_;
    static GetAsyncKeyState_t origin_GetAsyncKeyState_;
    static GetKeyState_t origin_GetKeyState_;
    static DirectInput8Create_t origin_DirectInput8Create_;
    static IsWindowVisibleHooked_t origin_IsWindowVisibleHooked_;
    static GetForegroundWindowHooked_t origin_GetForegroundWindowHooked_;
    static GetActiveWindow_t origin_GetActiveWindow_;
    static GetFocus_t origin_GetFocus_;
    static WindowFromPoint_t origin_WindowFromPoint_;
    static ClipCursor_t origin_ClipCursor_;
    static GetSystemMetrics_t origin_GetSystemMetrics_;
}

#endif //TC_APPLICATION_HOOK_MANAGER_H
