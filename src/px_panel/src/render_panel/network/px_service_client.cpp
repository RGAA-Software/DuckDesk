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
#include "px_common_new/connection_attempt_workflow.h"

namespace px
{

    const int kMaxClientQueuedMessage = 4096;

    PxServiceClient::PxServiceClient(const std::shared_ptr<PxApplication>& app) {
        statistics_ = PxStatistics::Instance();
        app_ = app;
        context_ = app_->GetContext();
    }

    PxServiceClient::~PxServiceClient() {
        Exit();
    }

    void PxServiceClient::Start() {
        if (exiting_ || client_) {
            return;
        }
        auto weak_self = weak_from_this();
        msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kState);
        msg_listener_->Listen<MsgGrTimer1S>([weak_self](const MsgGrTimer1S&) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            self->HeartBeat();
        });

        client_ = std::make_shared<asio2::ws_client>();
        connection_workflow_ = PxConnectionAttemptWorkflow::Create(
            context_->GetMessageNotifier()->GetAsyncRuntime(), std::chrono::seconds(10));
        if (!connection_workflow_) {
            LOGE("Cannot start Service connection workflow");
            return;
        }
        client_->set_auto_reconnect(true);
        client_->keep_alive(true);
        client_->set_timeout(std::chrono::milliseconds(2000));

        client_->bind_init([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_ || !self->connection_workflow_) {
                return;
            }
            self->client_->ws_stream().binary(true);
            self->client_->set_no_delay(true);
            const bool started = self->connection_workflow_->BeginAttempt(
                [weak_self](PxConnectionAttemptResult result) {
                    auto self = weak_self.lock();
                    if (!self || self->exiting_ || !self->context_) {
                        return;
                    }
                    if (!result) {
                        LOGW("Panel Service attempt ended: stage={}, code={}",
                             result.Error().stage, result.Error().StableCode());
                        return;
                    }
                    LOGI("Panel Service websocket ready, generation={}",
                         result.Value().generation);
                    self->context_->SendAppMessage(MsgConnectedToService{});
                    self->SendAuthInfo();
                });
            if (!started) {
                LOGW("Panel Service attempt was rejected during shutdown");
            }
        })
        .bind_connect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_ || !self->context_
                || !self->connection_workflow_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
                static_cast<void>(self->connection_workflow_->FailActive(MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected,
                    "panel-service.connect",
                    asio2::last_error_msg(),
                    true)));
                return;
            }
            LOGI("tcp connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
        })
        .bind_upgrade([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_ || !self->context_
                || !self->connection_workflow_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
                static_cast<void>(self->connection_workflow_->FailActive(MakePxAsyncError(
                    PxAsyncErrorCode::kProtocolError,
                    "panel-service.upgrade",
                    asio2::last_error_msg(),
                    true)));
                return;
            }
            LOGI("websocket upgrade success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
            static_cast<void>(self->connection_workflow_->MarkReady());
        })
        .bind_disconnect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->connection_workflow_) {
                return;
            }
            static_cast<void>(self->connection_workflow_->FailActive(MakePxAsyncError(
                PxAsyncErrorCode::kServiceNotConnected,
                "panel-service.disconnect",
                "Panel disconnected from Service",
                true)));
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
        if (exiting_.exchange(true)) {
            return;
        }
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        if (client_) {
            client_->stop_all_timers();
            client_->stop();
            client_.reset();
        }
        if (connection_workflow_) {
            connection_workflow_->Stop();
            connection_workflow_.reset();
        }
        statistics_.reset();
        context_.reset();
        app_.reset();
    }

    bool PxServiceClient::IsAlive() {
        return client_ && client_->is_started()
            && connection_workflow_ && connection_workflow_->IsReady();
    }

    void PxServiceClient::HeartBeat() {
        static int64_t hb_idx = 0;
        px::ServiceMessage msg;
        msg.set_type(ServiceMessageType::kSrvHeartBeat);
        auto sub = msg.mutable_heart_beat();
        sub->set_index(hb_idx++);
        sub->set_from("panel");
        FillAuthInfo(*sub->mutable_auth_info());
        PostNetMessage(msg.SerializeAsString());
    }

    void PxServiceClient::SendAuthInfo() {
        px::ServiceMessage msg;
        msg.set_type(ServiceMessageType::kSrvAuthInfo);
        FillAuthInfo(*msg.mutable_auth_info());
        PostNetMessage(msg.SerializeAsString());
    }

    void PxServiceClient::FillAuthInfo(MsgAuthInfo& auth_info) {
        auto settings = PxSettings::Instance();
        auth_info.set_device_id(settings->GetDeviceId());
        auth_info.set_console_host(settings->GetConsoleServerHost());
        auth_info.set_console_port(settings->GetConsoleServerPort());
        auth_info.set_console_ssl(settings->IsConsoleSslEnabled());
        auto companion = app_->GetCompanion();
        auto auth = companion ? companion->GetAuth() : nullptr;
        if (!auth) {
            // auth may not be pulled from server yet, send with empty auth fields
            return;
        }
        auth_info.set_auth_id(auth->auth_id_);
        auth_info.set_auth_name(auth->auth_name_);
        auth_info.set_machine_code(auth->machine_code_);
        auth_info.set_appkey(auth->appkey_);
        auth_info.set_role(static_cast<int>(auth->role_));
        auth_info.set_days(auth->days_);
        auth_info.set_max_streams(auth->max_streams_);
        auth_info.set_end_timestamp_ms(auth->end_timestamp_ms_);
    }

    void PxServiceClient::PostNetMessage(const std::string& msg) {
        if (IsAlive()) {
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
