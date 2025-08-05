//
// Created by RGAA on 2024-02-18.
//

#include "hook_manager.h"
#include "tc_common_new/data.h"
#include "client_ipc_manager.h"
#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "hk_video/shared_texture.h"
#include <Windows.h>
#include <detours/detours.h>
#include "tc_message.pb.h"
#include "app_shared_info_reader.h"
#include "ws_ipc_client.h"

namespace tc
{

    void HookManager::Init() {
        auto pid = GetCurrentProcessId();
        current_pid_ = pid;
        auto shm_name = std::format("application_shm_{}", pid);
        shared_info_reader_ = AppSharedInfoReader::Make(shm_name);
        app_shared_msg_ = shared_info_reader_->ReadData();
#if ENABLE_SHM
        client_ipc_mgr_ = ClientIpcManager::Make();
        client_ipc_mgr_->Init(pid, kHostToClientShmSize);
        client_ipc_mgr_->WaitForMessage();
        client_ipc_mgr_->RegisterHelloMessageCallback([=, this](std::shared_ptr<CaptureHelloMessage>&& msg) {
            LOGI("Hello msg : present:{}, present1: {}, resize: {}, release: {}", msg->dxgi_present, msg->dxgi_present1, msg->dxgi_resize, msg->dxgi_release);
        });
        client_ipc_mgr_->RegisterIpcMessageCallback([=, this](const std::shared_ptr<CaptureBaseMessage>& msg, const std::shared_ptr<Data>& data) {
            this->PushIpcMessage(msg);
            if (msg->type_ == kMouseEventMessage) {
                this->GenerateMouseEvent(msg);
            }
            else if (msg->type_ == kKeyboardEventMessage) {
                this->GenerateKeyboardEvent(msg);
            }
        });
#endif
        shared_texture_ = std::make_shared<SharedTexture>();
    }

    void HookManager::Send(std::shared_ptr<Data>&& data) {
#if ENABLE_SHM
        client_ipc_mgr_->Send(std::move(data));
#endif
    }

    void HookManager::Send(const std::string& msg) {
        ws_ipc_client_->PostIpcMessage(msg);
    }

    void HookManager::PushIpcMessage(const std::shared_ptr<CaptureBaseMessage>& msg) {
        if (messages_.Size() > 512) {
            messages_.PopFront();
        }
        messages_.PushBack(msg);
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
        //LOGI("HookedGetAsyncKeyState: {}", vKey);
        return origin_GetAsyncKeyState_(vKey);
    }

    SHORT HookedGetKeyState(int vKey) {
       // LOGI("HookedGetKeyState: {}", vKey);
        return origin_GetAsyncKeyState_(vKey);
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
        //LOGI("HookedIsWindowVisible");
        return true;
    }

    HWND WINAPI HookedGetForegroundWindow(VOID) {
        auto hwnd = HookManager::Instance()->ProcessHookedGetForegroundWindow();
        //LOGI("HookedGetForegroundWindow: {}", (void*)hwnd);
        return hwnd;
    }

    HWND HookedWindowFromPoint(_In_ POINT Point) {
        //LOGI("HookedWindowFromPoint: {},{}", Point.x, Point.y);
        return HookManager::Instance()->ProcessWindowFromPoint(Point);
    }

    BOOL HookedClipCursor(_In_opt_ CONST RECT *lpRect) {
        LOGI("ClipCursor");
        return TRUE; //origin_ClipCursor_(lpRect);//
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
        //GetForegroundWindow
        {
            origin_GetForegroundWindowHooked_ = GetProcAddressByName<GetForegroundWindowHooked_t>(L"User32", "GetForegroundWindow");
            auto r = DetourAttach(&(PVOID &)origin_GetForegroundWindowHooked_, &(PVOID &) HookedGetForegroundWindow);
            LOGI("Hook GetForegroundWindow result: {}", r);
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
        DetourTransactionCommit();
    }

    UINT HookManager::ProcessHookedGetRawInputData(
            HRAWINPUT hRawInput,
            UINT uiCommand,
            LPVOID pData,
            PUINT pcbSize,
            UINT cbSizeHeader) {

        if (uiCommand != RID_INPUT || hRawInput) {
            LOGI("ignore the message...1");
            return origin_GetRawInputData_(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
        }
        if (!pData) {
            if (!pcbSize) {
                return 0;
            }
            LOGI("Need a PostMessage... message size: {}", messages_.Size());
            *pcbSize = sizeof(RAWINPUT);
            return 0;
        }
        if (!pcbSize || *pcbSize < sizeof(RAWINPUT)) {
            LOGI("ignore the message...2");
            return -1;
        }

        if (messages_.Empty()) {
            return 0;//origin_GetRawInputData_(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
        }

        std::shared_ptr<CaptureBaseMessage> msg = messages_.Front();
        messages_.PopFront();

        auto raw_input = (RAWINPUT*)pData;
        memset(raw_input, 0, sizeof(RAWINPUT));
        if (msg->type_ == kKeyboardEventMessage) {
            auto keyboard_msg = std::static_pointer_cast<KeyboardEventMessage>(msg);
            bool down = keyboard_msg->down_;
            int k = keyboard_msg->key_;
            raw_input->header.dwType = RIM_TYPEKEYBOARD;
            raw_input->data.keyboard.VKey = k;
            raw_input->data.keyboard.MakeCode = MapVirtualKey(k, MAPVK_VK_TO_VSC);
            raw_input->data.keyboard.Flags = down ? RI_KEY_MAKE  : RI_KEY_BREAK;
            raw_input->data.keyboard.Message = down ? WM_KEYDOWN : WM_KEYUP;
            LOGI("vkey: {}, down: {}, makecode:{}", k, down, raw_input->data.keyboard.MakeCode);

        } else if (msg->type_ == kMouseEventMessage) {
            auto mouse_msg = std::static_pointer_cast<MouseEventMessage>(msg);

            raw_input->header.dwType = RIM_TYPEMOUSE;
            if (mouse_msg->absolute_) {
                raw_input->data.mouse.lLastX = mouse_msg->x_;
                raw_input->data.mouse.lLastY = mouse_msg->y_;
                raw_input->data.mouse.usFlags = MOUSE_MOVE_ABSOLUTE;
            }
            else {
                raw_input->data.mouse.lLastX = mouse_msg->delta_x_;
                raw_input->data.mouse.lLastY = mouse_msg->delta_y_;
                raw_input->data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
            }

            if (mouse_msg->data_) {
                raw_input->data.mouse.ulButtons |= RI_MOUSE_WHEEL;
                raw_input->data.mouse.usButtonData = mouse_msg->data_;
            }

            if (mouse_msg->pressed_) {
                if (mouse_msg->button_ == ButtonFlag::kLeftMouseButtonDown) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_LEFT_BUTTON_DOWN;
                }
                else if (mouse_msg->button_ == ButtonFlag::kMiddleMouseButtonDown) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_MIDDLE_BUTTON_DOWN;
                }
                else if (mouse_msg->button_ == ButtonFlag::kRightMouseButtonDown) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_RIGHT_BUTTON_DOWN;
                }
            }
            else if (mouse_msg->released_) {
                if (mouse_msg->button_ == ButtonFlag::kLeftMouseButtonUp) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_LEFT_BUTTON_UP;
                }
                else if (mouse_msg->button_ == ButtonFlag::kMiddleMouseButtonUp) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_MIDDLE_BUTTON_UP;
                }
                else if (mouse_msg->button_ == ButtonFlag::kRightMouseButtonUp) {
                    raw_input->data.mouse.ulButtons |= RI_MOUSE_RIGHT_BUTTON_UP;
                }
            }

#if 1
            LOGI("------------------------Replay-----------------------------");
            LOGI("button: {}, pressed: {}, released: {}", mouse_msg->button_, mouse_msg->pressed_, mouse_msg->released_);
            LOGI("uiCommand: {}, dwType: {}", uiCommand, raw_input->header.dwType);
            LOGI("usFlags: {}", raw_input->data.mouse.usFlags);
            LOGI("lLastX: {}, lLastY: {}", raw_input->data.mouse.lLastX, raw_input->data.mouse.lLastY);
            LOGI("ulButtons: {}, usButtonData: {}", raw_input->data.mouse.ulButtons, raw_input->data.mouse.usButtonData);
            //LOGI("cursor pos.x : {}, pos.y : {}", cursor_position.x, cursor_position.y);
            LOGI("........................Replay.............................");
#endif
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
        return hwnd_;
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

    void HookManager::GenerateMouseEvent(const std::shared_ptr<CaptureBaseMessage>& msg) {
        auto message = std::static_pointer_cast<MouseEventMessage>(msg);
        hwnd_ = (HWND)message->hwnd_;
        cursor_in_screen_position_.x = message->x_;
        cursor_in_screen_position_.y = message->y_;

        POINT client_area_point = {
            .x = (LONG)message->x_,
            .y = (LONG)message->y_,
        };
        ScreenToClient((HWND)hwnd_, &client_area_point);

        bool bVisible = (::GetWindowLong(hwnd_, GWL_STYLE) & WS_VISIBLE) != 0;
        bool v = IsWindowVisible(hwnd_);

        LOGI("handle: {:x}, Cursor in screen pos : {},{}  cursor in client pos: {},{}",
             message->hwnd_, cursor_in_screen_position_.x, cursor_in_screen_position_.y, client_area_point.x, client_area_point.y);

        auto hwnd = (HWND)message->hwnd_;
        static bool active = false;
        if (!active) {
            PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
            active = true;
        }
        //PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        BOOL bRet = PostMessage(hwnd, WM_SETFOCUS, 0, 0);

        /*
         * Key State Masks for Mouse Messages
         */
        DWORD mouse_key_state_flags = 0;
        UINT event = WM_MOUSEMOVE;
        if (message->pressed_) {
            if (message->button_ == ButtonFlag::kLeftMouseButtonDown) {
                event = WM_LBUTTONDOWN;
                mouse_key_state_flags = MK_LBUTTON;
                LOGI("Left mouse button pressed.....");
            }
            else if (message->button_ == ButtonFlag::kRightMouseButtonDown) {
                event = WM_RBUTTONDOWN;
                mouse_key_state_flags = MK_RBUTTON;
            }
            else if (message->button_ == ButtonFlag::kMiddleMouseButtonDown) {
                event = WM_MBUTTONDOWN;
                mouse_key_state_flags = MK_MBUTTON;
            }
        }
        else if (message->released_) {
            if (message->button_ == ButtonFlag::kLeftMouseButtonUp) {
                event = WM_LBUTTONUP;
                mouse_key_state_flags = MK_LBUTTON;
                LOGI("Left mouse button Release------");
            }
            else if (message->button_ == ButtonFlag::kRightMouseButtonUp) {
                event = WM_RBUTTONUP;
                mouse_key_state_flags = MK_RBUTTON;
            }
            else if (message->button_ == ButtonFlag::kMiddleMouseButtonUp) {
                event = WM_MBUTTONUP;
                mouse_key_state_flags = MK_MBUTTON;
            }
        }

        // UE4 滚轮消息模拟
        if (message->data_) {
            bRet = PostMessage(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, message->data_), MAKELPARAM(client_area_point.x, client_area_point.y));
        }

        PostMessage(hwnd, WM_MOUSEMOVE, mouse_key_state_flags, MAKELPARAM(client_area_point.x, client_area_point.y));
        PostMessage(hwnd, event, mouse_key_state_flags, MAKELPARAM(client_area_point.x, client_area_point.y));
        PostMessage(hwnd, WM_INPUT, 0, (LPARAM)NULL);
    }

    void HookManager::GenerateKeyboardEvent(const std::shared_ptr<CaptureBaseMessage>& m) {
        auto key_msg = std::static_pointer_cast<KeyboardEventMessage>(m);

        int msg;
        LPARAM lp = 0;
        if (key_msg->down_) {
            msg = WM_KEYDOWN;
        } else {
            // repeat count for the current message.
            lp = 1;
            // previous key state
            lp |= 1 << 30;
            // transition state
            lp |= 1 << 31;
            msg = WM_KEYUP;
        }

        PostMessage((HWND)key_msg->hwnd_, msg, key_msg->key_, lp);
        PostMessage((HWND)key_msg->hwnd_, WM_INPUT, 0, (LPARAM)NULL);
    }

    void HookManager::DumpSharedMessage() {
        LOGI("----Begin AppSharedMessage----");
        LOGI("ipc port: {}", app_shared_msg_->ipc_port_);
        LOGI("msg size: {}", app_shared_msg_->self_size_);
        LOGI("Hello msg : present:{:x}, present1: {:x}, resize: {:x}, release: {:x}",
             app_shared_msg_->dxgi_present, app_shared_msg_->dxgi_present1, app_shared_msg_->dxgi_resize, app_shared_msg_->dxgi_release);
        LOGI("----End AppSharedMessage----");
    }

    void HookManager::StartIpcClient() {
        ws_ipc_client_ = WsIpcClient::Make((int)app_shared_msg_->ipc_port_);
        ws_ipc_client_->Start();
        ws_ipc_client_->RegisterIpcMessageCallback([=, this](const std::shared_ptr<CaptureBaseMessage>& msg) {
            this->PushIpcMessage(msg);
            if (msg->type_ == kMouseEventMessage) {
                this->GenerateMouseEvent(msg);
            }
            else if (msg->type_ == kKeyboardEventMessage) {
                this->GenerateKeyboardEvent(msg);
            }
        });
    }
}