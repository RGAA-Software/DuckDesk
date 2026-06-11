//
// Created by RGAA on 1/08/2025.
//

#include "gr_panel_client.h"
#include <QString>
#include "tc_common_new/log.h"

namespace tc
{

    void GrPanelClient::Start() {
        exiting_ = false;
        auto weak_self = weak_from_this();
        client_ = std::make_shared<asio2::wss_client>();
        client_->set_auto_reconnect(true);
        client_->set_timeout(std::chrono::milliseconds(2000));

        client_->bind_init([weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_ && self->client_) {
                self->client_->ws_stream().binary(true);
                self->client_->set_no_delay(true);
                self->client_->ws_stream().set_option(
                    websocket::stream_base::decorator([](websocket::request_type &req) {
                        req.set(http::field::authorization, "websocket-client-authorization");}
                    )
                );
            }
        })
        .bind_connect([weak_self]() {
            if (asio2::get_last_error()) {
                //LOGE("WsPanelClient,connect failure : {} {}", asio2::last_error_val(), QString::fromStdString(asio2::last_error_msg()).toStdString().c_str());
            } else {
                if (auto self = weak_self.lock(); self && !self->exiting_ && self->client_) {
                    LOGI("WsPanelClient,connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
                }
            }
        })
        .bind_disconnect([]() {
            LOGE("WsPanelClient disconnected");
        })
        .bind_upgrade([weak_self]() {
            if (asio2::get_last_error()) {
                LOGE("WsPanelClient,upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
            }
        })
        .bind_recv([weak_self](std::string_view data) {

        });

        if (!client_->async_start("127.0.0.1", 20369, "/panel/renderer?from=guard")) {
            LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
    }

    void GrPanelClient::Exit() {
        exiting_ = true;
        if (client_) {
            client_->stop_all_timers();
            client_->stop();
        }
    }

}
