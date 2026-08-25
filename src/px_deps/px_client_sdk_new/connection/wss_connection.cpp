//
// Created by RGAA on 8/12/2024.
//

#include "wss_connection.h"
#include <asio2/websocket/wss_client.hpp>
#include <asio2/asio2.hpp>
#include "px_common_new/log.h"
#include "px_common_new/data.h"

namespace px
{

    WssConnection::WssConnection(const std::shared_ptr<ThunderSdkParams>& params,
                               const std::shared_ptr<MessageNotifier>& notifier,
                               const std::string& host,
                               int port,
                               const std::string& path) : Connection(params, notifier) {
        this->host_ = host;
        this->port_ = port;
        this->path_ = path;
    }

    WssConnection::~WssConnection() {
        Stop();
    }

    void WssConnection::Start() {
        const auto weak_self = weak_from_this();
        client_ = std::make_shared<asio2::wss_client>();
        client_->set_auto_reconnect(true);
        client_->set_timeout(std::chrono::milliseconds(2000));

        client_->bind_init([weak_self]() {
            const auto self = weak_self.lock();
            if (!self || !self->client_) return;
            self->client_->set_no_delay(true);
            self->client_->ws_stream().set_option(
                    websocket::stream_base::decorator([](websocket::request_type &req) {
                        req.set(http::field::authorization, "websocket-client-authorization");}
                    )
            );

        }).bind_connect([weak_self]() {
            const auto self = weak_self.lock();
            if (!self || !self->client_) return;
            if (asio2::get_last_error()) {
                LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            } else {
                LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
                self->client_->post_queued_event([weak_self]() {
                    if (const auto locked = weak_self.lock(); locked && locked->conn_cbk_) {
                        locked->conn_cbk_();
                    }
                });
            }
        }).bind_disconnect([weak_self]() {
            if (const auto self = weak_self.lock(); self && self->dis_conn_cbk_) {
                self->dis_conn_cbk_();
            }
        }).bind_upgrade([]() {
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
            }
        }).bind_recv([weak_self](std::string_view data) {
            if (const auto self = weak_self.lock(); self && self->msg_cbk_) {
                auto cpy_data = Data::Make(data.data(), data.size());
                self->msg_cbk_(cpy_data);
            }
        });

        // the /ws is the websocket upgraged target
        if (!client_->async_start(host_, port_, path_)) {
            LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
    }

    void WssConnection::Stop() {
        if (client_ && client_->is_started()) {
            client_->stop_all_timers();
            client_->stop();
        }
    }

    void WssConnection::PostBinaryMessage(std::shared_ptr<Data> msg) {
        if (client_ && client_->is_started()) {
            client_->ws_stream().binary(true);
            ++queuing_message_count_;
            const auto weak_self = weak_from_this();
            client_->async_send(msg->DataAddr(), msg->Size(), [weak_self]() {
                if (const auto self = weak_self.lock()) --self->queuing_message_count_;
            });
        }
    }

    void WssConnection::PostTextMessage(const std::string& msg) {
        if (client_ && client_->is_started()) {
            client_->ws_stream().text(true);
            ++queuing_message_count_;
            const auto weak_self = weak_from_this();
            client_->async_send(msg, [weak_self]() {
                if (const auto self = weak_self.lock()) --self->queuing_message_count_;
            });
        }
    }

    bool WssConnection::IsAlive() {
        return client_ && client_->is_started();
    }

}
