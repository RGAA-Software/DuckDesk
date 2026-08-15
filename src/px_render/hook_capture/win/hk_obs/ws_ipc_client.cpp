//
// Created by RGAA on 2024/3/17.
//

#include "ws_ipc_client.h"
#include <windows.h>
#include <format>
#include "px_common_new/log.h"
#include "px_capture_new/capture_message.h"

namespace px
{

    std::shared_ptr<WsIpcClient> WsIpcClient::Make(int port) {
        return std::make_shared<WsIpcClient>(port);
    }

    WsIpcClient::WsIpcClient(int port) {
        this->port_ = port;
    }

    void WsIpcClient::Start() {
        // Host net_ws listens with asio2::http_server (plain WS). Using wss_client here
        // previously made the injected DLL unable to connect to /ipc.
        ws_client_ = std::make_shared<asio2::ws_client>();
        ws_client_->set_auto_reconnect(true);
        ws_client_->set_timeout(std::chrono::seconds(2));
        ws_client_->bind_init([&]() {
            ws_client_->ws_stream().binary(true);
            ws_client_->set_no_delay(true);
        })
        .bind_connect([&]() {
            if (asio2::get_last_error()) {
                LOGE("ws ipc client connect failed: {} {}",
                     asio2::last_error_val(), asio2::last_error_msg());
            } else {
                LOGI("ws ipc client connected to 127.0.0.1:{}/ipc", port_);
            }
        })
        .bind_upgrade([&]() {
            if (asio2::get_last_error()) {
                LOGE("ws ipc upgrade failed: {}", asio2::last_error_msg());
            } else {
                LOGI("ws ipc upgrade success");
            }
        })
        .bind_disconnect([&]() {
            LOGW("ws ipc client disconnected from /ipc");
        })
        .bind_recv([&](std::string_view data) {
            this->DispatchIpcMessage(data);
        });

        // Identify ourselves by pid: the render only accepts /ipc connections from
        // pids it registered via RegisterIpcPid (wrote hook boot config for them).
        std::string ipc_path = std::format("/ipc?pid={}", GetCurrentProcessId());
        LOGI("ws ipc client starting: 127.0.0.1:{}{}", port_, ipc_path);
        if (!ws_client_->async_start("127.0.0.1", port_, ipc_path)) {
            LOGE("ws ipc async_start failure: {} {}",
                 asio2::last_error_val(), asio2::last_error_msg());
        }
    }

    void WsIpcClient::Exit() {
        if (ws_client_) {
            ws_client_->stop();
        }
    }

    void WsIpcClient::PostIpcMessage(const std::string& msg) {
        if (!ws_client_) {
            static uint64_t s_n = 0;
            if (++s_n == 1 || (s_n % 200) == 0) {
                LOGE("ws ipc PostIpcMessage: client is null, drop {} bytes n={}", msg.size(), s_n);
            }
            return;
        }
        if (!ws_client_->is_started()) {
            static uint64_t s_n = 0;
            if (++s_n == 1 || (s_n % 200) == 0) {
                LOGE("ws ipc PostIpcMessage: not started, drop {} bytes n={}", msg.size(), s_n);
            }
            return;
        }
        if (msg.empty()) {
            LOGE("ws ipc PostIpcMessage: empty payload");
            return;
        }
        ws_client_->async_send(msg);
    }

    void WsIpcClient::DispatchIpcMessage(std::string_view msg) {
        if (!ipc_cbk_ || msg.size() < sizeof(CaptureBaseMessage)) {
            return;
        }
        auto base_msg = (CaptureBaseMessage*)msg.data();
        if (base_msg->type_ == kMouseEventMessage) {
            if (msg.size() != sizeof(MouseEventMessage)) {
                LOGE("msg size != sizeof(MouseEventMessage), msg size: {}, event size: {}", msg.size(), sizeof(MouseEventMessage));
                return;
            }
            auto mem = std::make_shared<MouseEventMessage>();
            memcpy(mem.get(), msg.data(), msg.size());
            ipc_cbk_(mem);
        }
        else if (base_msg->type_ == kKeyboardEventMessage) {
            if (msg.size() != sizeof(KeyboardEventMessage)) {
                LOGE("msg size != sizeof(KeyboardEventMessage), msg size: {}, event size: {}", msg.size(), sizeof(KeyboardEventMessage));
                return;
            }
            auto kem = std::make_shared<KeyboardEventMessage>();
            memcpy(kem.get(), msg.data(), msg.size());
            ipc_cbk_(kem);
        }
        else if (base_msg->type_ == kCaptureResetInputMessage) {
            if (msg.size() != sizeof(CaptureResetInputMessage)) {
                LOGE("msg size != sizeof(CaptureResetInputMessage), msg size: {}", msg.size());
                return;
            }
            auto rim = std::make_shared<CaptureResetInputMessage>();
            memcpy(rim.get(), msg.data(), msg.size());
            ipc_cbk_(rim);
        }
    }

}
