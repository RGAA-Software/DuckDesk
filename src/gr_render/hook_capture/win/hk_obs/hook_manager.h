//
// Created by RGAA on 2024-02-18.
//

#ifndef TC_APPLICATION_HOOK_MANAGER_H
#define TC_APPLICATION_HOOK_MANAGER_H

#include <string>
#include <memory>
#include <functional>
#include <queue>
#include "tc_capture_new/capture_message.h"
#include "tc_common_new/concurrent_queue.h"
#include "hook_api.h"
#include <Windows.h>

namespace tc
{

    class Data;
    class SharedTexture;
    class AppSharedInfoReader;
    class AppSharedMessage;
    class WsIpcClient;

    class HookManager {
    public:

        static HookManager* Instance() {
            static HookManager hm;
            return &hm;
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
        void FocusSpoofWatcherMain();

    public:
        uint32_t current_pid_{};
        std::wstring dll_path_;
        std::shared_ptr<SharedTexture> shared_texture_ = nullptr;
        uint64_t frame_index_ = 0;

        tc::ConcurrentQueue<std::shared_ptr<CaptureBaseMessage>> messages_;
        POINT cursor_in_screen_position_;

        std::shared_ptr<AppSharedInfoReader> shared_info_reader_ = nullptr;
        std::shared_ptr<AppSharedMessage> app_shared_msg_ = nullptr;

        std::shared_ptr<WsIpcClient> ws_ipc_client_ = nullptr;

        // Set by mouse IPC from host (streaming input target).
        HWND hwnd_ = nullptr;
        // Auto-discovered top-level window of this process (multi-open audio).
        HWND own_game_hwnd_ = nullptr;
        HANDLE focus_spoof_watcher_ = nullptr;
        volatile long focus_spoof_stop_ = 0;
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
}

#endif //TC_APPLICATION_HOOK_MANAGER_H
