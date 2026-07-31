//
// Created by RGAA on 17/05/2025.
//

#include "ct_spvr_client.h"
#include "tc_common_new/log.h"
#include "ct_client_context.h"
#include "spvr_client.pb.h"
#include "thunder_sdk.h"
#include "tc_common_new/time_util.h"
#include "ct_auth_token.h"

namespace tc
{
    CtSpvrClient::CtSpvrClient(const std::shared_ptr<ThunderSdk>& sdk,
                               const std::shared_ptr<ClientContext>& ctx,
                               const std::string& host,
                               int port,
                               const std::string& device_id,
                               const std::string& remote_device_id,
                               const std::string& remote_device_ip,
                               const std::string& appkey) {
        sdk_ = sdk;
        context_ = ctx;
        host_ = host;
        port_ = port;
        device_id_ = device_id;
        remote_device_id_ = remote_device_id;
        remote_device_ip_ = remote_device_ip;
        appkey_ = appkey;
    }

    void CtSpvrClient::Start() {
        exiting_ = false;
        client_ = std::make_shared<asio2::wss_client>();
        client_->set_auto_reconnect(true);
        client_->keep_alive(true);
        client_->set_timeout(std::chrono::milliseconds(3000));
        client_->set_verify_mode(asio::ssl::verify_none);

        auto weak_self = weak_from_this();
        client_->start_timer("spvr_client_hb", std::chrono::seconds(1), [weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_ || !self->client_->is_started()) {
                return;
            }
            self->Heartbeat();
        });

        client_->bind_init([weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_ && self->client_) {
                self->client_->ws_stream().binary(true);
                self->client_->set_no_delay(true);

                // Generate a fresh token for every connection attempt (including auto reconnect).
                // The token has a short lifetime (60s), so reusing the original path on reconnect
                // would cause the CMS token filter to reject the connection.
                auto token = GenerateConnectionToken(self->appkey_);
                auto path = std::format("/spvr/client?appkey={}&token={}&ts={}&nonce={}&device_id={}&remote_device_id={}&remote_device_ip={}",
                    self->appkey_, token.token, token.ts, token.nonce, self->device_id_, self->remote_device_id_, self->remote_device_ip_);
                self->client_->set_upgrade_target(path);
            }

        })
        .bind_connect([weak_self]() {
            if (asio2::get_last_error()) {
                LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            } else {
                if (auto self = weak_self.lock(); self && !self->exiting_ && self->client_) {
                    LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
                }
            }

            if (auto self = weak_self.lock(); self && !self->exiting_ && self->client_) {
                self->client_->post_queued_event([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->Hello();
                    }
                });
            }

        })
        .bind_upgrade([weak_self]() {
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
            }
        })
        .bind_disconnect([weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                LOGE("*** Disconnected for spvr-client: {}", self->device_id_);
            }
        })
        .bind_recv([weak_self](std::string_view data) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                auto msg = std::string(data.data(), data.size());
                self->ParseMessage(msg);
            }
        });

        // the /ws is the websocket upgraged target
        // the concrete upgrade target (with a fresh token) is set in bind_init above
        LOGI("will connect => {}:{}/spvr/client", host_, port_);
        if (!client_->async_start(host_, port_)) {
            LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
    }

    void CtSpvrClient::Exit() {
        exiting_ = true;
        if (client_) {
            client_->stop_all_timers();
            client_->stop();
            client_.reset();
        }
    }

    bool CtSpvrClient::IsAlive() const {
        return client_ && client_->is_started();
    }

    void CtSpvrClient::Hello() {
        if (!IsAlive()) {
            return;
        }
        auto client = client_;
        if (!client) {
            return;
        }
        spvr_client::SpvrClientMessage msg;
        msg.set_msg_type(spvr_client::SpvrClientMessageType::kSpvrClientHello);
        msg.set_device_id(device_id_);
        const auto sub = msg.mutable_hello();
        sub->set_device_id(device_id_);
        client->async_send(msg.SerializeAsString());
    }


    void CtSpvrClient::Heartbeat() {
        if (!IsAlive()) {
            return;
        }
        auto client = client_;
        if (!client) {
            return;
        }
        auto sdk_last_hb_ts = sdk_->GetLastHeartbeatTimestamp();
        bool alive = (TimeUtil::GetCurrentTimestamp() - sdk_last_hb_ts) < 10'000;
        spvr_client::SpvrClientMessage msg;
        msg.set_msg_type(spvr_client::SpvrClientMessageType::kSpvrClientHeartBeat);
        msg.set_device_id(device_id_);
        const auto sub = msg.mutable_heartbeat();
        sub->set_hb_index(hb_index_++);
        sub->set_connection_alive(alive);
        client->async_send(msg.SerializeAsString());
    }

    void CtSpvrClient::ParseMessage(const std::string& data) {
        auto msg = std::make_shared<spvr_client::SpvrClientMessage>();
        if (!msg->ParseFromArray(data.data(), data.size())) {
            LOGE("CtSpvrClient parse message failed!");
            return;
        }
        if (msg->msg_type() == spvr_client::SpvrClientMessageType::kSpvrClientHeartBeat) {
            LOGI("Heartbeat: {}", msg->device_id(), msg->heartbeat().hb_index());
        }
    }

}
