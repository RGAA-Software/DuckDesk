//
// Created by RGAA  on 2024/1/18.
//

#include "desktop_capture.h"
#include "px_capture_new/capture_message.h"
#include "px_common_new/log.h"
#include "px_common_new/message_notifier.h"
#include <Shlobj.h>

namespace px
{

    DesktopCapture::DesktopCapture(const std::shared_ptr<MessageNotifier>& msg_notifier, const std::string& monitor) {
        msg_notifier_ = msg_notifier;
        capturing_monitor_name_ = monitor;
    }

    DesktopCapture::~DesktopCapture() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
    }

    void DesktopCapture::InitMessageListener() {
        msg_listener_ = msg_notifier_->CreateListener(MessageExecutionLane::kControl);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<RefreshScreenMessage>([weak_self](const RefreshScreenMessage&) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->RefreshScreen();
            LOGI("Refresh screen.");
        });
    }

    void DesktopCapture::SetCaptureMonitor(int index, const std::string& name) {
        std::lock_guard<std::mutex> lk(capturing_monitor_mtx_);
        capturing_monitor_index_ = index;
        capturing_monitor_name_ = name;
        refresh_screen_ = true;
    }

    void DesktopCapture::SetCaptureFps(int fps) {
        capture_fps_ = fps;
    }

    std::vector<CaptureMonitorInfo> DesktopCapture::GetCaptureMonitorInfo() {
        return sorted_monitors_;
    }

    void DesktopCapture::RefreshScreen() {
        SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_SENDCHANGE);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        HWND desktop = GetDesktopWindow();
        if (InvalidateRect(desktop, NULL, TRUE)) {
            UpdateWindow(desktop);
        }
        refresh_screen_ = true;
    }

    void DesktopCapture::SendCapturingMonitorMessage() {
        msg_notifier_->SendAppMessage(CaptureMonitorInfoMessage {
            .monitors_ = sorted_monitors_,
            .capturing_monitor_name_ = this->capturing_monitor_name_,
        });
    }

    int DesktopCapture::GetCapturingMonitorIndex() const {
        return capturing_monitor_index_;
    }

    std::string DesktopCapture::GetCapturingMonitorName() {
        return capturing_monitor_name_;
    }

}
