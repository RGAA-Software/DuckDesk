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
            self->websocket_upgraded_ = false;
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
        .bind_disconnect([weak_self]() {
            if (auto self = weak_self.lock()) {
                self->websocket_upgraded_ = false;
            }
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
            self->websocket_upgraded_ = true;
            LOGI("RenderServiceClient, websocket upgrade success");
            self->SendPendingAppInstanceReady();
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
            // Console stopped this instance: notify clients then exit gracefully
            LOGW("kSrvStopServer received from service, stopping render...");
            app_->OnServiceRequestedStop();
        }
        else if (sm.type() == ServiceMessageType::kSrvRedeemConnectionTicketResp) {
            const auto& sub = sm.redeem_connection_ticket_resp();
            std::function<void(bool, const std::string&, const std::vector<std::string>&,
                               const std::string&)> callback;
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
            callback(sub.ok(), sub.code(), permissions, sub.rtc_ice_config_json());
        }
        else if (sm.type() == ServiceMessageType::kSrvVirtualDisplayResult) {
            const auto& sub = sm.virtual_display_result();
            MsgVirtualDisplayServiceResult result;
            result.request_id_ = sub.request_id();
            result.accepted_ = sub.accepted();
            result.topology_changed_ = sub.topology_changed();
            result.topology_generation_ = sub.topology_generation();
            result.logical_display_id_ = sub.logical_display_id();
            result.error_code_ = sub.error_code();
            result.error_message_ = sub.error_message();
            result.owned_display_count_ = sub.owned_display_count();
            result.actual_usbmmidd_count_ = sub.actual_usbmmidd_count();
            result.driver_installed_ = sub.driver_installed();
            result.package_valid_ = sub.package_valid();
            result.removal_safe_ = sub.removal_safe();
            result.phase_ = sub.phase();

            std::function<void(const MsgVirtualDisplayServiceResult&)> callback;
            {
                std::scoped_lock lock(virtual_display_callbacks_mtx_);
                const auto it = virtual_display_callbacks_.find(result.request_id_);
                if (it != virtual_display_callbacks_.end()) {
                    callback = std::move(it->second);
                    virtual_display_callbacks_.erase(it);
                }
            }
            if (callback) {
                callback(result);
            }
            if (context_) {
                context_->PostTask([weak_self = weak_from_this(), result]() {
                    const auto self = weak_self.lock();
                    if (self && self->context_) {
                        self->context_->SendAppMessage(result);
                    }
                });
            }
        }
    }

    void RenderServiceClient::Exit() {
        auto client = std::move(client_);
        if (!client) return;
        // asio2::tcp_client::stop() is synchronous off its I/O thread and can
        // wait forever when an auto-reconnect handshake is being torn down.
        // Dispatch shutdown to the owning I/O thread; the captured shared_ptr
        // keeps the client alive until stop has completed there.
        client->post([client]() {
            client->set_auto_reconnect(false);
            client->stop_all_timers();
            client->stop();
        });
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

    void RenderServiceClient::NotifyAppInstanceReady(
        const std::string& instance_id, int listen_port, bool ok, const std::string& error) {
        {
            std::scoped_lock lock(ready_mtx_);
            ready_instance_id_ = instance_id;
            ready_listen_port_ = listen_port;
            ready_ok_ = ok;
            ready_error_ = error;
            ready_pending_ = true;
        }
        SendPendingAppInstanceReady();
    }

    void RenderServiceClient::SendPendingAppInstanceReady() {
        if (!websocket_upgraded_) return;
        px::ServiceMessage message;
        {
            std::scoped_lock lock(ready_mtx_);
            if (!ready_pending_) return;
            message.set_type(ServiceMessageType::kSrvAppInstanceReady);
            auto* ready = message.mutable_app_instance_ready();
            ready->set_instance_id(ready_instance_id_);
            ready->set_listen_port(ready_listen_port_);
            ready->set_ok(ready_ok_);
            ready->set_error(ready_error_);
            ready_pending_ = false;
        }
        PostNetMessage(message.SerializeAsString());
    }

    void RenderServiceClient::RedeemConnectionTicket(
        const std::string& ticket,
        const std::string& client_nonce,
        const std::string& instance_id,
        std::function<void(bool, const std::string&, const std::vector<std::string>&,
                           const std::string&)>&& callback) {
        if (!IsAlive() || ticket.empty() || client_nonce.empty()) {
            callback(false, "INVALID_ARGUMENT", {}, "");
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

    void RenderServiceClient::RequestVirtualDisplay(
        const std::string& request_id,
        int operation,
        uint32_t width,
        uint32_t height,
        uint32_t refresh_hz,
        std::function<void(const MsgVirtualDisplayServiceResult&)>&& callback) {
        if (!IsAlive() || request_id.empty() || operation < kVirtualDisplayCreate ||
            operation > kVirtualDisplayResetOwned) {
            MsgVirtualDisplayServiceResult result;
            result.request_id_ = request_id;
            result.error_code_ = "INVALID_ARGUMENT";
            result.error_message_ = "service is unavailable or virtual display request is invalid";
            callback(result);
            return;
        }
        bool duplicate = false;
        {
            std::scoped_lock lock(virtual_display_callbacks_mtx_);
            if (virtual_display_callbacks_.contains(request_id)) {
                duplicate = true;
            }
            else {
                virtual_display_callbacks_[request_id] = std::move(callback);
            }
        }
        // Never invoke an external callback while holding the callback-map lock.
        if (duplicate) {
            MsgVirtualDisplayServiceResult result;
            result.request_id_ = request_id;
            result.error_code_ = "REQUEST_IN_PROGRESS";
            result.error_message_ = "the same virtual display request is already pending";
            callback(result);
            return;
        }

        px::ServiceMessage message;
        message.set_type(ServiceMessageType::kSrvVirtualDisplayRequest);
        auto* request = message.mutable_virtual_display_request();
        request->set_request_id(request_id);
        request->set_operation(static_cast<VirtualDisplayOperation>(operation));
        request->set_width(width);
        request->set_height(height);
        request->set_refresh_hz(refresh_hz);
        PostNetMessage(message.SerializeAsString());
    }

}
