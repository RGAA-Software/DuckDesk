//
// Created by RGAA on 28/02/2025.
//

#include "relay_ws_client.h"
#include "tc_common_new/data.h"
#include "tc_common_new/log.h"
#include "tc_common_new/thread_util.h"
#include "tc_common_new/string_util.h"
#include <asio2/websocket/ws_client.hpp>
#include <asio2/asio2.hpp>
#include "relay_message.pb.h"

using namespace relay;

namespace tc
{

    RelayWsClient::RelayWsClient(const std::string& host, int port, const std::string& device_id,
                                 const std::string& device_name, const std::string& stream_id,
                                 const std::string& appkey, bool force_gdi)
                                 : RelayNetClient() {
        this->host_ = host;
        this->port_ = port;
        this->device_id_ = device_id;
        this->device_name_ = device_name;
        StringUtil::Replace(this->device_name_, " ", "");
        this->stream_id_ = stream_id;
        this->appkey_ = appkey;
        this->force_gdi_ = force_gdi;
    }

    RelayWsClient::~RelayWsClient() {

    }

    void RelayWsClient::Start() {
        client_ = std::make_shared<asio2::ws_client>();
        client_->set_auto_reconnect(true);
        client_->set_timeout(std::chrono::milliseconds(3000));
        client_->start_timer("ws-heartbeat", std::chrono::seconds(1), [thiz = std::weak_ptr(shared_from_this())]() {
            if (auto self = thiz.lock(); self) {
                self->HeartBeat();
            }
        });

        client_->bind_init([thiz = std::weak_ptr(shared_from_this())]() {
            if (auto self = thiz.lock(); self && self->client_) {
                self->client_->set_no_delay(true);
                self->client_->ws_stream().set_option(
                    websocket::stream_base::decorator([](websocket::request_type &req) {
                        req.set(http::field::authorization, "websocket-client-authorization");
                    })
                );
            }
        })
        .bind_connect([thiz = std::weak_ptr(shared_from_this())]() {
            if (auto self = thiz.lock(); self && self->client_) {
                if (asio2::get_last_error()) {
                    LOGE("connect failure : {} {} [ {} - {} - {}]",
                         asio2::last_error_val(), asio2::last_error_msg().c_str(), self->host_, self->port_, self->device_id_);
                } else {
                    LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
                    self->client_->post_queued_event([self]() {
                        if (self->srv_conn_cbk_) {
                            self->srv_conn_cbk_();
                        }
                        self->SendHello();
                    });
                }
            }
        })
        .bind_disconnect([thiz = std::weak_ptr(shared_from_this())]() {
            if (auto self = thiz.lock(); self && self->client_) {
                if (self->srv_dis_conn_cbk_) {
                    self->srv_dis_conn_cbk_();
                }
            }
        })
        .bind_upgrade([]() {
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
            }
        })
        .bind_recv([thiz = std::weak_ptr(shared_from_this())](std::string_view data) {
            if (auto self = thiz.lock(); self && self->client_ && self->msg_cbk_) {
                if (self->msg_cbk_) {
                    auto cpy_data = Data::Make(data.data(), data.size());
                    self->msg_cbk_(cpy_data);
                }
            }
        });

        // the /ws is the websocket upgraged target
        auto ws_path = std::format("/relay?device_id={}&device_name={}&stream_id={}&appkey={}",
                                   device_id_, device_name_, stream_id_, appkey_);
        LOGI("Will connect: {}:{}{}", host_, port_, ws_path);
        if (!client_->async_start(host_, port_, ws_path)) {
            LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
    }

    void RelayWsClient::Stop() {
        if (client_) {
            client_->stop_all_timers();
            client_->stop();
            client_ = nullptr;
        }
    }

    void RelayWsClient::PostBinaryMessage(const std::string &msg) {
        std::lock_guard<std::mutex> guard(send_mtx_);
        if (!IsAlive()) {
            return;
        }
        client_->ws_stream().binary(true);
        queuing_msg_count_++;
        client_->async_send(msg, [this]() {
            queuing_msg_count_--;
        });
    }

    bool RelayWsClient::IsAlive() {
        return client_ && client_->is_started();
    }

    void RelayWsClient::SyncDeviceId(const std::string& device_id) {
        this->device_id_ = device_id;
    }

    void RelayWsClient::SendHello() {
        if (!IsAlive()) {
            return;
        }
        RelayMessage rl_msg;
        rl_msg.set_type(RelayMessageType::kRelayHello);
        rl_msg.set_from_device_id(this->device_id_);
        auto sub = rl_msg.mutable_hello();
        for (const auto& info : net_info_) {
            auto ni = sub->mutable_net_info()->Add();
            ni->set_ip(info.ip_);
            ni->set_mac(info.mac_);
        }
        auto msg = rl_msg.SerializeAsString();
        PostBinaryMessage(msg);
    }

    void RelayWsClient::HeartBeat() {
        if (!IsAlive()) {
            return;
        }
        RelayMessage rl_msg;
        rl_msg.set_type(RelayMessageType::kRelayHeartBeat);
        rl_msg.set_from_device_id(this->device_id_);
        auto sub = rl_msg.mutable_heartbeat();
        static int64_t hb_ibx = 0;
        sub->set_index(hb_ibx++);
        for (const auto& info : net_info_) {
            auto ni = sub->mutable_net_info()->Add();
            ni->set_ip(info.ip_);
            ni->set_mac(info.mac_);
        }
        auto msg = rl_msg.SerializeAsString();
        PostBinaryMessage(msg);
    }

    void RelayWsClient::SetDeviceNetInfo(const std::vector<tc::RelayDeviceNetInfo>& info) {
        net_info_ = info;
    }

    int64_t RelayWsClient::GetQueuingMsgCount() {
        if (!IsAlive()) {
            return 0;
        }
        return std::max((int64_t)queuing_msg_count_, (int64_t)client_->get_pending_event_count());
    }

    void RelayWsClient::PostNetTask(std::function<void ()> &&task) {
        if (IsAlive()) {
            client_->post_queued_event([t = std::move(task)]() {
                t();
            });
        }
    }

}