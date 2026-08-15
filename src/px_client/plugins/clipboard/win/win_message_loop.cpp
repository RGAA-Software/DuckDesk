#include "win_message_loop.h"
#include <iostream>
#include <wtsapi32.h>
#include <ole2.h>
#include "px_common_new/log.h"
#include "win_message_window.h"

namespace tc
{

    constexpr char kWindowName[] = "GammaRay_client_MessageWindow";

    void CALLBACK WinMessageLoop::WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject,
                                               LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
        if (event == EVENT_SYSTEM_DESKTOPSWITCH) {
            std::cout << "Desktop switch event detected." << std::endl;
        }
    }

    std::shared_ptr<WinMessageLoop> WinMessageLoop::Make(ClientClipboardPlugin* plugin) {
        return std::make_shared<WinMessageLoop>(plugin);
    }

    WinMessageLoop::WinMessageLoop(ClientClipboardPlugin* plugin) {
        plugin_ = plugin;
    }

    WinMessageLoop::~WinMessageLoop() {

    }

    void WinMessageLoop::CreateMessageWindow() {
        //构造函数内 不能使用shared_from_this();
        message_window_ = WinMessageWindow::Make(plugin_, shared_from_this());
    }

    void WinMessageLoop::OnWinSessionChange(uint32_t message) {

    }

    void WinMessageLoop::OnDisplayDeviceChange() {

    }

    void WinMessageLoop::Start() {
        CreateMessageWindow();
        thread_ = std::thread(std::bind(&WinMessageLoop::ThreadFunc, this));
    }

    void WinMessageLoop::Stop() {
        LOGI("WinMessageLoop stopping...");
        message_window_->CloseWindow();
        if (thread_.joinable()) {
            thread_.join();
        }
        LOGI("WinMessageLoop stoped.");
    }

    void WinMessageLoop::ThreadFunc() {
        // Make this thread an STA so OleSetClipboard'ed data objects can be
        // marshaled cross-process (Explorer queries them on paste). Must run the
        // message pump below to dispatch those incoming COM calls.
        OleInitialize(nullptr);

        if (!message_window_->Create(kWindowName)) {
            LOGE("WinMessageLoop create window error.");
            return;
        }
        LOGI("WinMessageWindow create success");
        HWND hwnd = nullptr;
        hwnd = message_window_->GetHwnd();
        if (!hwnd) {
            LOGE("WinMessageLoop hwnd is nullptr.");
            return;
        }

        if (!AddClipboardFormatListener(hwnd)) {
            LOGE("AddClipboardFormatListener failed, error: {}", GetLastError());
            return;
        }
        LOGI("AddClipboardFormatListener ok, hwnd={}", reinterpret_cast<void*>(hwnd));

        /* 
        * Under certain circumstances, even with administrator privileges, this function still fails to set and returns an error indicating insufficient permissions. 
        * In the clipboard plugin, there is no need to monitor session-related messages, so this part has been commented out.
        if (!WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_ALL_SESSIONS)) {
            LOGE("WTSRegisterSessionNotification error: {}", GetLastError());
            return;
        }
        */

        HWINEVENTHOOK hEventHook = SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH, nullptr, &WinMessageLoop::WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

        if (hEventHook == nullptr) {
            std::cout << "Failed to set event hook." << std::endl;
        }

        int bRet = 0;
        MSG msg{};
        while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) {
            if (bRet == -1) {
                break;
            }
            else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        RemoveClipboardFormatListener(hwnd);
        if (hEventHook != nullptr) {
            UnhookWinEvent(hEventHook);
        }

        OleUninitialize();
    }

    void WinMessageLoop::PostTask(std::function<void()> task) {
        if (message_window_) {
            message_window_->PostTask(std::move(task));
        }
    }

}