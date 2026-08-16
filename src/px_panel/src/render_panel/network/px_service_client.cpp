//
// Created by RGAA on 2024-04-20.
//

#include "px_service_client.h"
#include "render_panel/px_context.h"
#include "render_panel/px_statistics.h"
#include "px_common_new/log.h"
#include "px_common_new/message_notifier.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/px_settings.h"
#include "render_panel/companion/panel_companion.h"
#include "px_service_message.pb.h"

namespace px
{

    const int kMaxClientQueuedMessage = 4096;

    PxServiceClient::PxServiceClient(const std::shared_ptr<PxApplication>& app) {
        statistics_ = PxStatistics::Instance();
        app_ = app;
        context_ = app_->GetContext();
    }

    void PxServiceClient::Start() {
        auto weak_self = weak_from_this();
        msg_listener_ = context_->GetMessageNotifier()->CreateListener();
        msg_listener_->Listen<MsgGrTimer1S>([weak_self](const MsgGrTimer1S& msg) {
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

        })
        .bind_connect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || !self->client_ || !self->context_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            } else {
                LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
            }

            self->context_->PostTask([weak_self]() {
                auto self = weak_self.lock();
                if (!self || !self->context_) {
                    return;
                }
                self->context_->SendAppMessage(MsgConnectedToService{});
            });

            self->SendAuthInfo();

        })
        .bind_upgrade([]() {
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
            }
        })
        .bind_recv([weak_self](std::string_view data) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            auto msg = std::string(data.data(), data.size());
            self->ParseMessage(msg);
        });

        if (!client_->async_start("127.0.0.1", 20375, "/service/message?from=panel")) {
            LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
        }
    }

    void PxServiceClient::ParseMessage(const std::string& msg) {
        px::ServiceMessage sm;
        try {
            sm.ParseFromString(msg);
        }
        catch(...) {
            LOGE("ParseMessage failed!");
            return;
        }

        if (sm.type() == ServiceMessageType::kSrvHeartBeatResp) {
            const auto& sub = sm.heart_beat_resp();
            auto hb_idx = sub.index();
            auto is_render_alive = sub.render_status() == RenderStatus::kWorking;
            //LOGI("hb_idx: {}, is render alive: {}", hb_idx, is_render_alive);
            context_->SendAppMessage(MsgServerAlive {
                .alive_ = is_render_alive,
            });
        }
    }

    void PxServiceClient::Exit() {
        if (client_) {
            client_->stop();
        }
    }

    bool PxServiceClient::IsAlive() {
        return client_ && client_->is_started();
    }

    void PxServiceClient::HeartBeat() {
        static int64_t hb_idx = 0;
        px::ServiceMessage msg;
        msg.set_type(ServiceMessageType::kSrvHeartBeat);
        auto sub = msg.mutable_heart_beat();
        sub->set_index(hb_idx++);
        sub->set_from("panel");
        FillAuthInfo(sub->mutable_auth_info());
        PostNetMessage(msg.SerializeAsString());
    }

    void PxServiceClient::SendAuthInfo() {
        px::ServiceMessage msg;
        msg.set_type(ServiceMessageType::kSrvAuthInfo);
        FillAuthInfo(msg.mutable_auth_info());
        PostNetMessage(msg.SerializeAsString());
    }

    void PxServiceClient::FillAuthInfo(MsgAuthInfo* auth_info) {
        if (!auth_info) {
            return;
        }
        auto settings = PxSettings::Instance();
        auth_info->set_device_id(settings->GetDeviceId());
        auth_info->set_cms_host(settings->GetCmsServerHost());
        auth_info->set_cms_port(settings->GetCmsServerPort());
        auto companion = app_->GetCompanion();
        auto auth = companion ? companion->GetAuth() : nullptr;
        if (!auth) {
            // auth may not be pulled from server yet, send with empty auth fields
            return;
        }
        auth_info->set_auth_id(auth->auth_id_);
        auth_info->set_auth_name(auth->auth_name_);
        auth_info->set_machine_code(auth->machine_code_);
        auth_info->set_appkey(auth->appkey_);
        auth_info->set_role(static_cast<int>(auth->role_));
        auth_info->set_days(auth->days_);
        auth_info->set_max_streams(auth->max_streams_);
        auth_info->set_end_timestamp_ms(auth->end_timestamp_ms_);
    }

    void PxServiceClient::PostNetMessage(const std::string& msg) {
        if (client_ && client_->is_started()) {
            if (queuing_message_count_ > kMaxClientQueuedMessage) {
                LOGW("too many message in queue, discard the message in PxServiceClient");
                return;
            }
            queuing_message_count_++;
            auto weak_self = weak_from_this();
            client_->async_send(msg, [weak_self]() {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                self->queuing_message_count_--;
            });
        }
    }

}
