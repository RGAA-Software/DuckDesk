//
// Created by RGAA on 8/12/2024.
//

#include "ws_local_client.h"
#include <asio2/websocket/ws_client.hpp>
#include <asio2/asio2.hpp>
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"

namespace tc
{

    WsLocalClient::WsLocalClient(const std::string& host, int port, const std::string& path) {
        this->host_ = host;
        this->port_ = port;
        this->path_ = path;
    }

    WsLocalClient::~WsLocalClient() {

    }

    void WsLocalClient::Start() {
        client_ = std::make_shared<asio2::ws_client>();
        client_->set_auto_reconnect(true);
        client_->set_timeout(std::chrono::milliseconds(2000));

        client_->bind_init([=, this]() {
            client_->set_no_delay(true);
            client_->ws_stream().set_option(
                    websocket::stream_base::decorator([](websocket::request_type &req) {
                        req.set(http::field::authorization, "websocket-client-authorization");}
                    )
            );

        }).bind_connect([=, this]() {
            if (asio2::get_last_error()) {
                LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            } else {
                LOGI("connect success : {} {} ", client_->local_address().c_str(), client_->local_port());
                client_->post_queued_event([=, this]() {

                });
            }
        }).bind_disconnect([this]() {

        }).bind_upgrade([]() {
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
            }
        }).bind_recv([=, this](std::string_view data) {

        });

        // the /ws is the websocket upgraged target
        if (!client_->async_start(this->host_, this->port_, this->path_)) {
            LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
    }

    void WsLocalClient::Stop() {
        if (client_ && client_->is_started()) {
            client_->stop_all_timers();
            client_->stop();
        }
    }

    void WsLocalClient::PostBinaryMessage(std::shared_ptr<Data> msg) {
        if (client_ && client_->is_started()) {
            client_->ws_stream().binary(true);
            queuing_message_count_++;
            client_->async_send(msg->DataAddr(), msg->Size(), [this]() {
                queuing_message_count_--;
            });
        }
    }

}
