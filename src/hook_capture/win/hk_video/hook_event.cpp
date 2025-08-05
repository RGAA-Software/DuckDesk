#include "hook_event.h"

#include "tc_common_new/log.h"
#include <Winuser.h>

#define DEBUG_EVENT 0

namespace tc
{

    HookEvent::HookEvent() {
    }

    HookEvent::~HookEvent() {
        api_HookGetRawInputData.Unhook();
        api_HookGetRawInputBuffer.Unhook();
        api_HookPostMessageA.Unhook();
        api_HookPostMessageW.Unhook();
        api_HookSendMessageA.Unhook();
        api_HookSendMessageW.Unhook();
    }

    void HookEvent::HookGetRawInputBuffer() {

    }

    UINT HookEvent::ProcessHookedGetRawInputData(
            HRAWINPUT hRawInput,
            UINT uiCommand,
            LPVOID pData,
            PUINT pcbSize,
            UINT cbSizeHeader) {

        if (uiCommand != RID_INPUT || hRawInput) {
            //LOGI("uiCommand : %p, hRawInput : %p", uiCommand, hRawInput);
            return origin_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
        }

        if (!pData && pcbSize) {
            *pcbSize = sizeof(RAWINPUT);
            LOGI("pcbSize : {}", *pcbSize);
            return 0;
        }

        if (messages.size() <= 0) {
            //LOGI("No message for RawInput.");
            return -1;
        }

        std::shared_ptr<IPCBaseMessage> msg = messages.front();
        if (!msg) {
            return -1;
        }
        messages.pop();

        //LOGI("HookedGetRawInputData, pData : %p, uiCommand %p, pcbSize : %d, cbSizeHeader : %d", pData, uiCommand, *pcbSize, cbSizeHeader);

        if (!pData) {
            LOGI("ProcessHookedGetRawInputData not have data !");
            return -1;
        }
// USE____
//		if (msg->type == IPCMessageType::kSharedMouseEvent) {
//			auto mouse_msg = std::static_pointer_cast<IPCMouseMessage>(msg);
//			RAWINPUT* raw_input = (RAWINPUT*)pData;
//			memset(raw_input, 0, sizeof(RAWINPUT));
//
//			raw_input->header.dwType = RIM_TYPEMOUSE;
//			if (false) {
//				raw_input->data.mouse.lLastX = mouse_msg->mouse_x;
//				raw_input->data.mouse.lLastY = mouse_msg->mouse_y;
//				raw_input->data.mouse.usFlags = MOUSE_MOVE_ABSOLUTE;
//			}
//			else {
//				raw_input->data.mouse.lLastX = mouse_msg->mouse_dx;
//				raw_input->data.mouse.lLastY = mouse_msg->mouse_dy;
//				raw_input->data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
//			}
//
//			cursor_position.x = mouse_msg->mouse_x;//
//			cursor_position.y = mouse_msg->mouse_y;//
//
//			//cursor_position.x += mouse_msg->mouse_dx;
//			//cursor_position.y += mouse_msg->mouse_dy;
//
//			if (mouse_msg->middle_scroll) {
//				raw_input->data.mouse.ulButtons |= RI_MOUSE_WHEEL;
//				raw_input->data.mouse.usButtonData = mouse_msg->middle_scroll;
//			}
//
//			if (mouse_msg->pressed) {
//				if (mouse_msg->key == MouseKey::kLeft) {
//					raw_input->data.mouse.ulButtons |= RI_MOUSE_LEFT_BUTTON_DOWN;
//				}
//				else if (mouse_msg->key == MouseKey::kMiddle) {
//					raw_input->data.mouse.ulButtons |= RI_MOUSE_MIDDLE_BUTTON_DOWN;
//				}
//				else if (mouse_msg->key == MouseKey::kRight) {
//					raw_input->data.mouse.ulButtons |= RI_MOUSE_RIGHT_BUTTON_DOWN;
//				}
//			}
//			else if (mouse_msg->released) {
//				if (mouse_msg->key == MouseKey::kLeft) {
//					raw_input->data.mouse.ulButtons |= RI_MOUSE_LEFT_BUTTON_UP;
//				}
//				else if (mouse_msg->key == MouseKey::kMiddle) {
//					raw_input->data.mouse.ulButtons |= RI_MOUSE_MIDDLE_BUTTON_UP;
//				}
//				else if (mouse_msg->key == MouseKey::kRight) {
//					raw_input->data.mouse.ulButtons |= RI_MOUSE_RIGHT_BUTTON_UP;
//				}
//			}
//
//			//LOGI("------------------------Replay-----------------------------");
//			//LOGI("uiCommand: %p, dwType: 0x%x", uiCommand, raw_input->header.dwType);
//			//LOGI("usFlags: %d", raw_input->data.mouse.usFlags);
//			//LOGI("lLastX: %d, lLastY: %d", raw_input->data.mouse.lLastX, raw_input->data.mouse.lLastY);
//			//LOGI("ulButtons: 0x%x, usButtonData: %d", raw_input->data.mouse.ulButtons, raw_input->data.mouse.usButtonData);
//			//LOGI("cursor pos.x : %d, pos.y : %d", cursor_position.x, cursor_position.y);
//			//LOGI("........................Replay.............................");
//
//			return sizeof(RAWINPUT);
//		}
// USE____

        return origin_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    }

    BOOL HookEvent::ProcessHookedGetCursorPos(LPPOINT lpPoint) {
        if (!lpPoint) {
            return false;
        }
        BOOL ret = false;
        //auto ret = origin_GetCursorPos(lpPoint);
        if (lpPoint) {
            lpPoint->x = cursor_position.x;
            lpPoint->y = cursor_position.y;
            ret = true;
        }
        //LOGI("GetCursorPos : %d, %d", lpPoint->x, lpPoint->y);
        return ret;
    }

    UINT WINAPI HookedGetRawInputData(
            HRAWINPUT hRawInput,
            UINT uiCommand,
            LPVOID pData,
            PUINT pcbSize,
            UINT cbSizeHeader) {

#if DEBUG_EVENT
        auto ret = origin_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
        if (uiCommand == RID_INPUT && pData) {
            RAWINPUT* raw_input = (RAWINPUT*)pData;
            if (raw_input->header.dwType == RIM_TYPEMOUSE) {
                if (raw_input->data.mouse.ulButtons & RI_MOUSE_LEFT_BUTTON_DOWN || raw_input->data.mouse.ulButtons & RI_MOUSE_LEFT_BUTTON_UP) {
                    LOGI("-----------------------------------------------------");
                    LOGI("uiCommand: %p, dwType: 0x%x", uiCommand, raw_input->header.dwType);
                    LOGI("usFlags: %d", raw_input->data.mouse.usFlags);
                    LOGI("lLastX: %d, lLastY: %d", raw_input->data.mouse.lLastX, raw_input->data.mouse.lLastY);
                    LOGI("ulButtons: 0x%x, usButtonData: %d", raw_input->data.mouse.ulButtons, raw_input->data.mouse.usButtonData);
                    LOGI(".....................................................");
                }
            }
            else if (raw_input->header.dwType == RIM_TYPEKEYBOARD) {
                LOGI("-----------------------------------------------------");
                LOGI("Keyboard, vk : %d, pressed : %d", raw_input->data.keyboard.VKey, raw_input->data.keyboard.Flags);
                LOGI("-----------------------------------------------------");
            }
        }
        return ret;
#else
        return HookEvent::Instance()->ProcessHookedGetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
#endif
    }

    UINT WINAPI HookedGetRawInputBuffer(
            PRAWINPUT pData,
            PUINT pcbSize,
            UINT cbSizeHeader) {
        LOGI("HookedGetRawInputBuffer: {}", *pcbSize);
        return origin_GetRawInputBuffer(pData, pcbSize, cbSizeHeader);
    }

    BOOL HookedPostMessageA(
            HWND hWnd,
            UINT Msg,
            WPARAM wParam,
            LPARAM lParam) {
        //LOGI("PostMessageA: %d", Msg);
        return origin_PostMessageA(hWnd, Msg, wParam, lParam);
    }

    BOOL HookedPostMessageW(
            HWND hWnd,
            UINT Msg,
            WPARAM wParam,
            LPARAM lParam) {
        //LOGI("PostMessageW: %d", Msg);
        return origin_PostMessageW(hWnd, Msg, wParam, lParam);
    }

    LRESULT HookedSendMessageA(
            HWND hWnd,
            UINT Msg,
            WPARAM wParam,
            LPARAM lParam) {
        LOGI("HookedSendMessageA : {}", Msg);
        return origin_SendMessageA(hWnd, Msg, wParam, lParam);
    }

    LRESULT HookedSendMessageW(
            HWND hWnd,
            UINT Msg,
            WPARAM wParam,
            LPARAM lParam) {
        LOGI("HookedSendMessageW : {}", Msg);
        return origin_SendMessageW(hWnd, Msg, wParam, lParam);
    }

    BOOL WINAPI HookedGetCursorPos(LPPOINT lpPoint) {
        return HookEvent::Instance()->ProcessHookedGetCursorPos(lpPoint);
    }

    BOOL WINAPI HookedSetCursorPos(int X, int Y) {
        LOGI("SetCursorPos: {}, {}", X, Y);
        return origin_SetCursorPos(X, Y);
    }

    void HookEvent::Init() {

        NTSTATUS status;
        origin_GetRawInputData = (GetRawInputData_t) GetProcAddress(GetModuleHandle(TEXT("User32")),
                                                                    "GetRawInputData");
        status = tc::HookAllThread(api_HookGetRawInputData, origin_GetRawInputData, HookedGetRawInputData);
        if (!NT_SUCCESS(status)) {
            LOGE("HOOK ERROR : GetRawInputData");
            return;
        }

        origin_GetRawInputBuffer = (GetRawInputBuffer_t) GetProcAddress(GetModuleHandle(TEXT("User32")),
                                                                        "GetRawInputBuffer");
        status = tc::HookAllThread(api_HookGetRawInputBuffer, origin_GetRawInputBuffer, HookedGetRawInputBuffer);
        if (!NT_SUCCESS(status)) {
            LOGE("HOOK ERROR : GetRawInputBuffer");
            return;
        }

        origin_PostMessageA = (PostMessageA_t) GetProcAddress(GetModuleHandle(TEXT("User32")), "PostMessageA");
        status = tc::HookAllThread(api_HookPostMessageA, origin_PostMessageA, HookedPostMessageA);
        if (!NT_SUCCESS(status)) {
            LOGE("HOOK ERROR : PostMessageA");
            return;
        }

        origin_PostMessageW = (PostMessageW_t) GetProcAddress(GetModuleHandle(TEXT("User32")), "PostMessageW");
        status = tc::HookAllThread(api_HookPostMessageW, origin_PostMessageW, HookedPostMessageW);
        if (!NT_SUCCESS(status)) {
            LOGE("HOOK ERROR : PostMessageW");
            return;
        }

        origin_SendMessageA = (SendMessageA_t) GetProcAddress(GetModuleHandle(TEXT("User32")), "SendMessageA");
        status = tc::HookAllThread(api_HookSendMessageA, origin_SendMessageA, HookedSendMessageA);
        if (!NT_SUCCESS(status)) {
            LOGE("HOOK ERROR : SendMessageA");
            return;
        }

        origin_SendMessageW = (SendMessageA_t) GetProcAddress(GetModuleHandle(TEXT("User32")), "SendMessageW");
        status = tc::HookAllThread(api_HookSendMessageW, origin_SendMessageW, HookedSendMessageW);
        if (!NT_SUCCESS(status)) {
            LOGE("HOOK ERROR : SendMessageW");
            return;
        }

        origin_GetCursorPos = (GetCursorPos_t) GetProcAddress(GetModuleHandle(TEXT("User32")), "GetCursorPos");
        status = tc::HookAllThread(api_HookGetCursorPos, origin_GetCursorPos, HookedGetCursorPos);
        if (!NT_SUCCESS(status)) {
            LOGE("HOOK ERROR : GetCursorPos");
            return;
        }

        origin_SetCursorPos = (SetCursorPos_t) GetProcAddress(GetModuleHandle(TEXT("User32")), "SetCursorPos");
        status = tc::HookAllThread(api_HookSetCursorPos, origin_SetCursorPos, HookedSetCursorPos);
        if (!NT_SUCCESS(status)) {
            LOGE("HOOK ERROR : SetCursorPos");
            return;
        }

        LOGI("HookEvent init success.");
    }

    void HookEvent::ReceiveIPCMessages() {
//		auto conn = InterCommClient::Instance();
//		conn->SetOnMouseMessageCallback([=](std::shared_ptr<IPCMouseMessage> message) {
//			auto base_msg = std::static_pointer_cast<IPCBaseMessage>(message);
//			messages.push(base_msg);
//			ProcessMouseMessage(message);
//		});
//
//		conn->SetOnKeyboardMessageCallback([=](std::shared_ptr<IPCKeyboardMessage> message) {
//			ProcessKeyboardMessage(message);
//		});
    }

    static std::pair<USHORT, bool> VirtualKeyToScanCode(UINT VirtualKey) {
        USHORT ScanCode = (USHORT) MapVirtualKey(VirtualKey, MAPVK_VK_TO_VSC);
        bool IsExtended = false;

        // because MapVirtualKey strips the extended bit for some keys
        switch (VirtualKey) {
            case VK_RMENU:
            case VK_RCONTROL:
            case VK_LEFT:
            case VK_UP:
            case VK_RIGHT:
            case VK_DOWN: // arrow keys
            case VK_PRIOR:
            case VK_NEXT: // page up and page down
            case VK_END:
            case VK_HOME:
            case VK_INSERT:
            case VK_DELETE:
            case VK_DIVIDE: // numpad slash
            case VK_NUMLOCK: {
                IsExtended = true;
                break;
            }
        }

        return std::make_pair(ScanCode, IsExtended);
    }

    static LPARAM
    CreateLPARAM_KeyUpDown(UINT VirtualKey, USHORT RepeatCount, bool TransitionState, bool PreviousKeyState,
                           bool ContextCode) {
        std::pair<USHORT, bool> ScanCode = VirtualKeyToScanCode(VirtualKey);
        LOGI("VK : {}, SC : {}", VirtualKey, ScanCode.first);
        return (
                (LPARAM(TransitionState) << 31) |
                (LPARAM(PreviousKeyState) << 30) |
                (LPARAM(ContextCode) << 29) |
                (LPARAM(ScanCode.second) << 24) |
                (LPARAM(ScanCode.first) << 16) |
                LPARAM(RepeatCount)
        );
    }

    static LPARAM CreateLPARAM_KeyDown(UINT VirtualKey, USHORT RepeatCount = 1) {
        return CreateLPARAM_KeyUpDown(VirtualKey, RepeatCount, false, RepeatCount > 1, false);
    }

    static LPARAM CreateLPARAM_KeyUp(UINT VirtualKey) {
        return CreateLPARAM_KeyUpDown(VirtualKey, 1, true, true, false);
    }

    void HookEvent::ProcessKeyboardMessage(std::shared_ptr<IPCKeyboardMessage> message) {
#if 0 // USE____
        auto hwnd = (HWND)message->hwnd;
        if (!hwnd) {
            return;
        }

        //static bool active = false;
        //if (!active) {
        //	PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        //	active = true;
        //}
        //PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        //BOOL bRet = PostMessage(hwnd, WM_SETFOCUS, 0, 0);

        uint32_t vk = message->vk;
        bool pressed = message->pressed;
        uint32_t msg = pressed ? WM_KEYDOWN : WM_KEYUP;

        static bool active1 = false;
        if (!active1) {
            PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
            active1 = true;
        }
        PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        BOOL bRet = PostMessage(hwnd, WM_SETFOCUS, 0, 0);

        LOGI("VK :  {}, pressed : {}, msg : {}", vk, pressed, msg);
        LPARAM param;
        if (pressed) {
            param = CreateLPARAM_KeyDown(vk);
        }
        else {
            param = CreateLPARAM_KeyUp(vk);
        }
        PostMessage(hwnd, msg, vk, param);
#endif // USE____
    }

    void HookEvent::ProcessMouseMessage(std::shared_ptr<IPCMouseMessage> message) {
#if 0 // USE____
        if (cursor_position.x == kInvalidCursorPos && cursor_position.y == kInvalidCursorPos) {
            cursor_position.x = message->mouse_x;
            cursor_position.y = message->mouse_y;
        }

        LOGI("Cursor pos : {} {} ", message->mouse_x, message->mouse_y);

        auto hwnd = (HWND)message->hwnd;
        static bool active = false;
        if (!active) {
            PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
            active = true;
        }
        PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        BOOL bRet = PostMessage(hwnd, WM_SETFOCUS, 0, 0);

        /*
         * Key State Masks for Mouse Messages
         */
        DWORD mouse_key_state_flags = 0;
        UINT event = WM_MOUSEMOVE;
        if (message->pressed) {
            if (message->key == MouseKey::kLeft) {
                event = WM_LBUTTONDOWN;
                mouse_key_state_flags = MK_LBUTTON;
            }
            else if (message->key == MouseKey::kRight) {
                event = WM_RBUTTONDOWN;
                mouse_key_state_flags = MK_RBUTTON;
            }
            else if (message->key == MouseKey::kMiddle) {
                event = WM_MBUTTONDOWN;
                mouse_key_state_flags = MK_MBUTTON;
            }
        }
        else if (message->released) {
            if (message->key == MouseKey::kLeft) {
                event = WM_LBUTTONUP;
                mouse_key_state_flags = MK_LBUTTON;
            }
            else if (message->key == MouseKey::kRight) {
                event = WM_RBUTTONUP;
                mouse_key_state_flags = MK_RBUTTON;
            }
            else if (message->key == MouseKey::kMiddle) {
                event = WM_MBUTTONUP;
                mouse_key_state_flags = MK_MBUTTON;
            }
        }

        PostMessage(hwnd, event, mouse_key_state_flags, MAKELPARAM(cursor_position.x, cursor_position.y));

        PostMessage(hwnd, WM_INPUT, 0, (LPARAM)NULL);
#endif
    }

}