//
// Created by RGAA on 2024/3/5.
//

#include "ws_ipc_router.h"
#include "tc_common_new/data.h"
#include "tc_common_new/log.h"
#include "tc_capture_new/capture_message.h"
#include "rd_app.h"

namespace tc
{

    void WsIpcRouter::OnOpen(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnOpen(sess_ptr);
    }

    void WsIpcRouter::OnClose(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnClose(sess_ptr);
    }

    void
    WsIpcRouter::OnMessage(std::shared_ptr<asio2::http_session> &sess_ptr, int64_t socket_fd, std::string_view data) {
        WsRouter::OnMessage(sess_ptr, socket_fd, data);
        if (data.size() < sizeof(CaptureBaseMessage)) {
            LOGE("IPC message too small: {}", data.size());
            return;
        }
        auto base_msg = (CaptureBaseMessage *) data.data();
        std::shared_ptr<RdApplication> app;
        try {
            app = Get<std::shared_ptr<RdApplication>>("app");
        } catch (...) {
            app = rdApp;
        }
        if (!app) {
            LOGE("IPC video frame dropped: RdApplication not available");
            return;
        }
        if (base_msg->type_ == kCaptureVideoFrame) {
            auto msg = std::make_shared<CaptureVideoFrame>();
            if (data.size() != sizeof(CaptureVideoFrame)) {
                LOGE("Error size of ipc video frame, data size: {}, CaptureVideoFrame size: {}", data.size(),
                     sizeof(CaptureVideoFrame));
                return;
            }
            memcpy(msg.get(), data.data(), data.size());
            app->OnIpcVideoFrame(msg);
        }
    }

    void WsIpcRouter::OnPing(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnPing(sess_ptr);
    }

    void WsIpcRouter::OnPong(std::shared_ptr<asio2::http_session> &sess_ptr) {
        WsRouter::OnPong(sess_ptr);
    }

    void WsIpcRouter::PostBinaryMessage(std::shared_ptr<Data> data) {
        session_->async_send(data->AsString());
    }

    void WsIpcRouter::PostBinaryMessage(const std::string &data) {
        if (session_ && session_->is_started()) {
            queuing_message_count_++;
            auto weak_self = std::enable_shared_from_this<WsIpcRouter>::weak_from_this();
            session_->async_send(data, [weak_self](size_t byte_sent) {
                if (auto router = weak_self.lock()) {
                    router->queuing_message_count_--;
                }
            });
        }
    }
}
