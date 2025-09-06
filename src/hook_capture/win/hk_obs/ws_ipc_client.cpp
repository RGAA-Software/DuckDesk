//
// Created by RGAA on 2024/3/17.
//

#include "ws_ipc_client.h"
#include "tc_common_new/log.h"
#include "tc_capture_new/capture_message.h"

namespace tc
{

    std::shared_ptr<WsIpcClient> WsIpcClient::Make(int port) {
        return std::make_shared<WsIpcClient>(port);
    }

    WsIpcClient::WsIpcClient(int port) {
        this->port_ = port;
    }

    void WsIpcClient::Start() {
        wss_client_ = std::make_shared<asio2::wss_client>();
        wss_client_->set_connect_timeout(std::chrono::seconds(2));
        wss_client_->bind_init([&]() {
            wss_client_->ws_stream().binary(true);
        })
        .bind_connect([&]() {
            if (asio2::get_last_error()) {
                LOGI("ws client connect msg: {}", asio2::get_last_error_msg());
            } else {
                LOGI("ws client connect msg success.");
            }
        })
        .bind_upgrade([&]() {
            if (asio2::get_last_error()) {
                LOGI("update failed : {}", asio2::get_last_error_msg());
            } else {
                const websocket::response_type &rep = wss_client_->get_upgrade_response();
                auto it = rep.find(http::field::authentication_results);
                if (it != rep.end()) {
                    beast::string_view auth = it->value();
                    LOGI("auth == 200 OK ? : {}", (auth == "200 OK"));
                }
                LOGI("update success...");
            }
        })
        .bind_recv([&](std::string_view data) {
//                auto msg = std::make_shared<tc::Message>();
//                auto ok = msg->ParseFromArray(data.data(), data.size());
//                if (!ok) {
//                    LOGI("Parse to ipc message failed. recv data size: {}", (int) data.size());
//                    return;
//                }
            this->DispatchIpcMessage(data);
        });

        LOGI("will start at :{}, {}", port_, "/ipc");
        if (!wss_client_->async_start("127.0.0.1", port_, "/ipc")) {
            LOGI("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
        LOGI("After start ws client....");
    }

    void WsIpcClient::Exit() {
        if (wss_client_) {
            wss_client_->stop();
        }
    }

    void WsIpcClient::PostIpcMessage(const std::string& msg) {
        if (!wss_client_) {
            LOGE("ws ipc client is null.");
            return;
        }
        wss_client_->async_send(msg);
    }

    void WsIpcClient::DispatchIpcMessage(std::string_view msg) {
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
                LOGE("msg size != sizeof(KeyboardEventMessage), msg size: {}, event size: {}", msg.size(), sizeof(MouseEventMessage));
                return;
            }
            auto kem = std::make_shared<KeyboardEventMessage>();
            memcpy(kem.get(), msg.data(), msg.size());
            ipc_cbk_(kem);
        }
    }

}