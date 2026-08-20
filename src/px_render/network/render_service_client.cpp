//
// Created by RGAA on 2024-04-20.
//

#include "render_service_client.h"

#include <px_common_new/string_util.h>

#include "rd_context.h"
#include "rd_statistics.h"
#include "px_common_new/log.h"
#include "px_common_new/message_notifier.h"
#include "rd_app.h"
#include "app/app_messages.h"
#include "px_service_message.pb.h"
#include "settings/rd_settings.h"

namespace px
{

    const int kMaxClientQueuedMessage = 4096;

    RenderServiceClient::RenderServiceClient(const std::shared_ptr<RdApplication>& app) {
        statistics_ = RdStatistics::Instance();
        app_ = app;
        context_ = app_->GetContext();
    }

    void RenderServiceClient::Start() {
        auto weak_self = weak_from_this();
        msg_listener_ = context_->GetMessageNotifier()->CreateListener();
        msg_listener_->Listen<MsgTimer1000>([weak_self](const MsgTimer1000& msg) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->HeartBeat();
        });

        client_ = std::make_shared<asio2::ws_client>();
        client_->set_auto_reconnect(true);
        client_->keep_alive(true);
        client_->set_timeout(std::chrono::milliseconds(2000));

        client_->bind_init([weak_self]() {
            auto self = weak_self.lock();
            if (!self || !self->client_) {
                return;
            }
            self->client_->ws_stream().binary(true);
            self->client_->set_no_delay(true);
            const auto ipc_token = RdSettings::Instance()->service_ipc_token_;
            self->client_->ws_stream().set_option(
                websocket::stream_base::decorator([ipc_token](websocket::request_type &req) {
                    req.set(http::field::authorization, "Bearer " + ipc_token);}
                )
            );
        })
        .bind_connect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || !self->client_ || !self->context_) {
                return;
            }
            if (asio2::get_last_error()) {
                auto wstr = StringUtil::ToWString(asio2::last_error_msg());
                auto str = StringUtil::ToUTF8(wstr);
                LOGE("RenderServiceClient, connect failure : {} {}", asio2::last_error_val(), str);
                return;
            }
            LOGI("RenderServiceClient, tcp connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
        })
        .bind_disconnect([]() {
            LOGE("RenderServiceClient disconnected");
        })
        .bind_upgrade([weak_self]() {
            auto self = weak_self.lock();
            if (!self || !self->context_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("RenderServiceClient, upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
                return;
            }
            LOGI("RenderServiceClient, websocket upgrade success");
            self->context_->PostTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || !self->context_) {
                    return;
                }
                self->context_->SendAppMessage(MsgRenderConnected2Service{});
            });
        })
        .bind_recv([weak_self](std::string_view data) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            const auto msg = std::string(data.data(), data.size());
            self->ParseMessage(msg);
        });

        auto settings = RdSettings::Instance();
        LOGI("Will connect to service : {}:{}", settings->service_server_host_, settings->service_server_port_);
        // the /ws is the websocket upgraged target
        if (!client_->async_start(settings->service_server_host_, settings->service_server_port_, "/service/message?from=render")) {
            LOGE("RenderServiceClient, connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
        else {
            LOGE("RenderServiceClient, connect websocket server start successful.");
        }
    }

    void RenderServiceClient::ParseMessage(const std::string& msg) {
        px::ServiceMessage sm;
        try {
            sm.ParseFromString(msg);
        }
        catch(...) {
            LOGE("ParseMessage failed!");
            return;
        }

        if (sm.type() == ServiceMessageType::kSrvHeartBeatResp) {
            auto sub = sm.heart_beat_resp();
            auto hb_idx = sub.index();
            auto is_render_alive = sub.render_status() == RenderStatus::kWorking;
            //LOGI("hb_idx: {}, is render alive: {}", hb_idx, is_render_alive);
        }
        else if (sm.type() == ServiceMessageType::kSrvStopServer) {
            // CMS stopped this instance: notify clients then exit gracefully
            LOGW("kSrvStopServer received from service, stopping render...");
            app_->OnServiceRequestedStop();
        }
        else if (sm.type() == ServiceMessageType::kSrvRedeemConnectionTicketResp) {
            const auto& sub = sm.redeem_connection_ticket_resp();
            std::function<void(bool, const std::string&, const std::vector<std::string>&)> callback;
            {
                std::scoped_lock lock(ticket_callbacks_mtx_);
                const auto it = ticket_callbacks_.find(sub.request_id());
                if (it == ticket_callbacks_.end()) {
                    return;
                }
                callback = std::move(it->second);
                ticket_callbacks_.erase(it);
            }
            std::vector<std::string> permissions;
            if (sub.has_grant()) {
                permissions.assign(sub.grant().permissions().begin(), sub.grant().permissions().end());
            }
            callback(sub.ok(), sub.code(), permissions);
        }
    }

    void RenderServiceClient::Exit() const {
        if (client_) {
            client_->stop();
        }
    }

    bool RenderServiceClient::IsAlive() const {
        return client_ != nullptr && client_->is_started();
    }

    void RenderServiceClient::HeartBeat() {
        static int64_t hb_idx = 0;
        px::ServiceMessage msg;
        msg.set_type(ServiceMessageType::kSrvHeartBeat);
        auto sub = msg.mutable_heart_beat();
        sub->set_index(hb_idx++);
        sub->set_from(std::format("render_{}", RdSettings::Instance()->transmission_.listening_port_));
        PostNetMessage(msg.SerializeAsString());
    }

    void RenderServiceClient::PostNetMessage(const std::string& msg) {
        if (client_ && client_->is_started()) {
            if (queuing_message_count_ > kMaxClientQueuedMessage) {
                LOGW("too many message in queue, discard the message in RenderServiceClient");
                return;
            }
            ++queuing_message_count_;
            auto weak_self = weak_from_this();
            client_->async_send(msg, [weak_self]() {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                --self->queuing_message_count_;
            });
        }
    }

    void RenderServiceClient::RedeemConnectionTicket(
        const std::string& ticket,
        const std::string& client_nonce,
        const std::string& instance_id,
        std::function<void(bool, const std::string&, const std::vector<std::string>&)>&& callback) {
        if (!IsAlive() || ticket.empty() || client_nonce.empty()) {
            callback(false, "INVALID_ARGUMENT", {});
            return;
        }
        const auto request_id = std::format(
            "render-{}-{}",
            RdSettings::Instance()->transmission_.listening_port_,
            ++ticket_request_seq_);
        {
            std::scoped_lock lock(ticket_callbacks_mtx_);
            ticket_callbacks_[request_id] = std::move(callback);
        }
        px::ServiceMessage message;
        message.set_type(ServiceMessageType::kSrvRedeemConnectionTicket);
        auto* request = message.mutable_redeem_connection_ticket();
        request->set_request_id(request_id);
        request->set_ticket(ticket);
        request->set_client_nonce(client_nonce);
        request->set_instance_id(instance_id);
        PostNetMessage(message.SerializeAsString());
    }

}
