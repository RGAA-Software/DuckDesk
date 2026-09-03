#include "win_message_loop.h"
#include <iostream>
#include <wtsapi32.h>
#include <ole2.h>
#include <utility>
#include "px_common_new/log.h"
#include "win_message_window.h"

namespace px
{

    constexpr char kWindowName[] = "PxClient_MessageWindow";

    void CALLBACK WinMessageLoop::WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject,
                                               LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
        if (event == EVENT_SYSTEM_DESKTOPSWITCH) {
            std::cout << "Desktop switch event detected." << std::endl;
        }
    }

    std::shared_ptr<WinMessageLoop> WinMessageLoop::Make(
        ClipboardUpdatedCallback clipboard_updated_callback) {
        return std::make_shared<WinMessageLoop>(
            std::move(clipboard_updated_callback));
    }

    WinMessageLoop::WinMessageLoop(
        ClipboardUpdatedCallback clipboard_updated_callback)
        : clipboard_updated_callback_(
              std::move(clipboard_updated_callback)) {
    }

    WinMessageLoop::~WinMessageLoop() {
        Stop();
    }

    void WinMessageLoop::CreateMessageWindow() {
        //构造函数内 不能使用shared_from_this();
        message_window_ =
            WinMessageWindow::Make(clipboard_updated_callback_);
    }

    void WinMessageLoop::OnWinSessionChange(uint32_t message) {

    }

    void WinMessageLoop::OnDisplayDeviceChange() {

    }

    bool WinMessageLoop::Start() {
        if (thread_.joinable()) {
            return true;
        }
        CreateMessageWindow();
        const auto message_window = message_window_;
        const auto startup_signal = std::make_shared<std::promise<bool>>();
        auto startup_result = startup_signal->get_future();
        thread_ = std::jthread([message_window, startup_signal]() {
            ThreadFunc(message_window, startup_signal);
        });
        const bool started = startup_result.get();
        if (!started && thread_.joinable()) {
            thread_.join();
            message_window_.reset();
        }
        return started;
    }

    void WinMessageLoop::Stop() {
        LOGI("WinMessageLoop stopping...");
        if (message_window_) {
            message_window_->CloseWindow();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        message_window_.reset();
        LOGI("WinMessageLoop stoped.");
    }

    void WinMessageLoop::ThreadFunc(
        const std::shared_ptr<WinMessageWindow>& message_window,
        const std::shared_ptr<std::promise<bool>>& startup_signal) {
        // Make this thread an STA so OleSetClipboard'ed data objects can be
        // marshaled cross-process (Explorer queries them on paste). Must run the
        // message pump below to dispatch those incoming COM calls.
        OleInitialize(nullptr);

        if (!message_window || !message_window->Create(kWindowName)) {
            LOGE("WinMessageLoop create window error.");
            startup_signal->set_value(false);
            OleUninitialize();
            return;
        }
        LOGI("WinMessageWindow create success");
        HWND hwnd = nullptr;
        hwnd = message_window->GetHwnd();
        if (!hwnd) {
            LOGE("WinMessageLoop hwnd is nullptr.");
            startup_signal->set_value(false);
            OleUninitialize();
            return;
        }

        if (!AddClipboardFormatListener(hwnd)) {
            LOGE("AddClipboardFormatListener failed, error: {}", GetLastError());
            DestroyWindow(hwnd);
            startup_signal->set_value(false);
            OleUninitialize();
            return;
        }
        startup_signal->set_value(true);
        LOGI("AddClipboardFormatListener ok, hwnd={}", reinterpret_cast<void*>(hwnd));

        /* 
        * Under certain circumstances, even with administrator privileges, this function still fails to set and returns an error indicating insufficient permissions. 
        * The clipboard module does not need to monitor session-related messages,
        * so this part remains disabled.
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
