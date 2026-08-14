//
// Created by RGAA on 8/12/2024.
//

#include "ws_connection.h"
#include <asio2/websocket/ws_client.hpp>
#include <asio2/asio2.hpp>
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"

namespace tc
{

    WsConnection::WsConnection(const std::shared_ptr<ThunderSdkParams>& params,
                               const std::shared_ptr<MessageNotifier>& notifier,
                               const std::string& host,
                               int port,
                               const std::string& path) : Connection(params, notifier) {
        this->host_ = host;
        this->port_ = port;
        this->path_ = path;
    }

    WsConnection::~WsConnection() {

    }

    void WsConnection::Start() {
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
                    if (conn_cbk_) {
                        conn_cbk_();
                    }
                });
            }
        }).bind_disconnect([this]() {
            if (dis_conn_cbk_) {
                dis_conn_cbk_();
            }
        }).bind_upgrade([]() {
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
            }
        }).bind_recv([=, this](std::string_view data) {
            if (msg_cbk_) {
                auto cpy_data = Data::Make(data.data(), data.size());
                msg_cbk_(cpy_data);
            }
        });

        // the /ws is the websocket upgraged target
        if (!client_->async_start(this->host_, this->port_, this->path_)) {
            LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
    }

    void WsConnection::Stop() {
        if (client_ && client_->is_started()) {
            client_->stop_all_timers();
            client_->stop();
        }
    }

    void WsConnection::PostBinaryMessage(std::shared_ptr<Data> msg) {
        if (client_ && client_->is_started()) {
            client_->ws_stream().binary(true);
            ++queuing_message_count_;
            client_->async_send(msg->DataAddr(), msg->Size(), [this]() {
                --queuing_message_count_;
            });
        }
    }

    void WsConnection::PostTextMessage(const std::string& msg) {
        if (client_ && client_->is_started()) {
            client_->ws_stream().text(true);
            ++queuing_message_count_;
            client_->async_send(msg, [this]() {
                --queuing_message_count_;
            });
        }
    }

    bool WsConnection::IsAlive() {
        return client_ && client_->is_started();
    }

}
