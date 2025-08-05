#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <winuser.h>
#include <dxgi1_4.h>

#include <queue>
#include <memory>

#include "easyhook/easyhook.h"
#include "hk_utils/hook_api.hpp"

constexpr auto kInvalidCursorPos = -1020304050;

#pragma comment(lib, "User32.lib")

namespace tc
{

    class IPCBaseMessage;
    class IPCMouseMessage;
    class IPCKeyboardMessage;

    typedef UINT (WINAPI *GetRawInputData_t)(
            HRAWINPUT hRawInput,
            UINT uiCommand,
            LPVOID pData,
            PUINT pcbSize,
            UINT cbSizeHeader);

    typedef UINT (WINAPI *GetRawInputBuffer_t)(
            PRAWINPUT pData,
            PUINT pcbSize,
            UINT cbSizeHeader);

    typedef BOOL(WINAPI *PostMessageA_t)(
            HWND hWnd,
            UINT Msg,
            WPARAM wParam,
            LPARAM lParam);

    typedef BOOL(WINAPI *PostMessageW_t)(
            HWND hWnd,
            UINT Msg,
            WPARAM wParam,
            LPARAM lParam);

    typedef LRESULT(WINAPI *SendMessageA_t)(
            HWND hWnd,
            UINT Msg,
            WPARAM wParam,
            LPARAM lParam);

    typedef LRESULT(WINAPI *SendMessageW_t)(
            HWND hWnd,
            UINT Msg,
            WPARAM wParam,
            LPARAM lParam);

    typedef BOOL (WINAPI *GetCursorPos_t)(LPPOINT lpPoint);

    typedef BOOL (WINAPI *SetCursorPos_t)(int X, int Y);

    class HookEvent {
    public:

        static HookEvent *Instance() {
            static HookEvent ins;
            return &ins;
        }

        HookEvent();

        ~HookEvent();

        void HookGetRawInputBuffer();

        UINT ProcessHookedGetRawInputData(
                HRAWINPUT hRawInput,
                UINT uiCommand,
                LPVOID pData,
                PUINT pcbSize,
                UINT cbSizeHeader);

        BOOL ProcessHookedGetCursorPos(LPPOINT lpPoint);

        void Init();

        void ReceiveIPCMessages();

        void ProcessMouseMessage(std::shared_ptr<IPCMouseMessage> message);

        void ProcessKeyboardMessage(std::shared_ptr<IPCKeyboardMessage> message);

    private:

        tc::HookApi api_HookGetRawInputBuffer;
        tc::HookApi api_HookGetRawInputData;
        tc::HookApi api_HookPostMessageA;
        tc::HookApi api_HookPostMessageW;
        tc::HookApi api_HookSendMessageA;
        tc::HookApi api_HookSendMessageW;
        tc::HookApi api_HookGetCursorPos;
        tc::HookApi api_HookSetCursorPos;

        // messages
        std::queue<std::shared_ptr<IPCBaseMessage>> messages;

        POINT cursor_position{kInvalidCursorPos, kInvalidCursorPos};
    };

    static GetRawInputBuffer_t origin_GetRawInputBuffer;
    static GetRawInputData_t origin_GetRawInputData;
    static PostMessageA_t origin_PostMessageA;
    static PostMessageW_t origin_PostMessageW;
    static SendMessageA_t origin_SendMessageA;
    static SendMessageW_t origin_SendMessageW;
    static GetCursorPos_t origin_GetCursorPos;
    static SetCursorPos_t origin_SetCursorPos;
}