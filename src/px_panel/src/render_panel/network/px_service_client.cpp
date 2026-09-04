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
#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/reconnect_supervisor.h"
#include "px_common_new/websocket_reconnect_adapter.h"

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
        std::unique_lock operation_lock(operation_mutex_, std::try_to_lock);
        if (!operation_lock.owns_lock()) {
            return;
        }
        {
            std::lock_guard lock(network_mutex_);
            if (exiting_ || client_) {
                return;
            }
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

        const auto runtime = context_->GetMessageNotifier()->GetAsyncRuntime();
        const auto client = std::make_shared<asio2::ws_client>();
        const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
        const auto supervisor = PxReconnectSupervisor::Create(runtime, MakeWebSocketReconnectOptions("panel_service"));
        if (!client || !scope || !supervisor) {
            LOGE("event=module.start component=panel_service code=ASYNC_WORKFLOW_CREATE_FAILED "
                 "operation=start_client outcome=failed recoverable=false");
            operation_lock.unlock();
            Exit();
            return;
        }
        {
            std::lock_guard lock(network_mutex_);
            async_runtime_ = runtime;
            client_ = client;
            connection_scope_ = scope;
            reconnect_supervisor_ = supervisor;
        }
        client->set_auto_reconnect(false);
        client->keep_alive(true);
        client->set_timeout(std::chrono::milliseconds(2000));

        client->bind_init([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_ || !self->reconnect_supervisor_) {
                return;
            }
            self->client_->ws_stream().binary(true);
            self->client_->set_no_delay(true);
        })
        .bind_connect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_ || !self->context_
                || !self->reconnect_supervisor_) {
                return;
            }
            if (asio2::get_last_error()) {
                static_cast<void>(self->reconnect_supervisor_->FailActive(self->callback_generation_.load(), MakePxAsyncError(
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
                || !self->reconnect_supervisor_) {
                return;
            }
            if (asio2::get_last_error()) {
                static_cast<void>(self->reconnect_supervisor_->FailActive(self->callback_generation_.load(), MakePxAsyncError(
                    PxAsyncErrorCode::kProtocolError,
                    "panel-service.upgrade",
                    asio2::last_error_msg(),
                    true)));
                return;
            }
            LOGI("websocket upgrade success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
            static_cast<void>(self->reconnect_supervisor_->MarkReady(self->callback_generation_.load()));
        })
        .bind_disconnect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->reconnect_supervisor_) {
                return;
            }
            static_cast<void>(self->reconnect_supervisor_->MarkDisconnected(self->callback_generation_.load(), MakePxAsyncError(
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

        PxReconnectSupervisorHooks hooks{
            .start_attempt = [weak_self, client](const std::uint64_t generation) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return PxResult<void>::Failure(MakePxAsyncError(
                        PxAsyncErrorCode::kServiceStopped, "panel-service.start", "Panel Service owner is stopping"));
                }
                self->callback_generation_.store(generation, std::memory_order_release);
                return StartWebSocketAdapter(client, "127.0.0.1", 20375, "/service/message?from=panel", "panel-service.start");
            },
            .stop_attempt = [client](const std::chrono::steady_clock::time_point deadline) {
                return StopWebSocketAdapter(client, deadline, "panel-service.retry-reset");
            },
            .on_ready = [weak_self](std::uint64_t) {
                if (const auto self = weak_self.lock(); self && !self->exiting_ && self->context_) {
                    self->context_->SendAppMessage(MsgConnectedToService{});
                    self->SendAuthInfo();
                }
            },
        };
        if (!scope->Spawn("panel-service-reconnect", [supervisor, hooks = std::move(hooks)]() mutable {
                return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
            })) {
            LOGE("event=module.start component=panel_service code=ASYNC_SCOPE_SPAWN_FAILED "
                 "operation=start_reconnect outcome=failed recoverable=false");
            operation_lock.unlock();
            Exit();
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
        std::unique_lock operation_lock(operation_mutex_, std::try_to_lock);
        if (!operation_lock.owns_lock()) {
            return;
        }
        const auto first_exit = !exiting_.exchange(true);
        if (first_exit && msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        std::shared_ptr<asio2::ws_client> client;
        std::shared_ptr<PxAsyncScope> scope;
        std::shared_ptr<PxReconnectSupervisor> supervisor;
        {
            std::lock_guard lock(network_mutex_);
            client = client_;
            scope = connection_scope_;
            supervisor = reconnect_supervisor_;
        }
        if (supervisor) {
            supervisor->Stop();
        }
        const auto result = StopWebSocketConnectionBlocking(client, scope, std::chrono::seconds(5), "panel-service.stop");
        if (result.deferred) {
            ScheduleDeferredExit();
            return;
        }
        if (!result.Succeeded()) {
            LOGE("event=async.scope_drain component=panel_service code=ASYNC_SCOPE_DRAIN_TIMEOUT operation=exit "
                 "outcome=timeout recoverable=false scope_drained={} adapter_stopped={} outstanding={}",
                 result.scope_drained, result.adapter_stopped, scope ? scope->GetStatistics().outstanding : 0);
            return;
        }
        {
            std::lock_guard lock(network_mutex_);
            client_.reset();
            reconnect_supervisor_.reset();
            connection_scope_.reset();
            async_runtime_.reset();
            deferred_exit_scheduled_ = false;
        }
        statistics_.reset();
        context_.reset();
        app_.reset();
    }

    bool PxServiceClient::IsAlive() {
        std::lock_guard lock(network_mutex_);
        return !exiting_ && client_ && client_->is_started() && reconnect_supervisor_ && reconnect_supervisor_->IsReady();
    }

    std::shared_ptr<asio2::ws_client> PxServiceClient::ClientSnapshot() const {
        std::lock_guard lock(network_mutex_);
        return client_;
    }

    void PxServiceClient::ScheduleDeferredExit() {
        if (deferred_exit_scheduled_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::shared_ptr<PxAsyncRuntime> runtime;
        {
            std::lock_guard lock(network_mutex_);
            runtime = async_runtime_;
        }
        const auto weak_self = weak_from_this();
        if (!runtime || !runtime->DeferBlocking([weak_self] {
                if (const auto self = weak_self.lock()) {
                    self->Exit();
                }
            })) {
            deferred_exit_scheduled_ = false;
            LOGE("event=async.scope_drain component=panel_service code=ASYNC_DEFER_FAILED operation=exit "
                 "outcome=failed recoverable=false");
        }
    }

    void PxServiceClient::HeartBeat() {
        px::ServiceMessage msg;
        msg.set_type(ServiceMessageType::kSrvHeartBeat);
        auto sub = msg.mutable_heart_beat();
        sub->set_index(heartbeat_index_.fetch_add(1, std::memory_order_acq_rel));
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
        const auto client = ClientSnapshot();
        if (IsAlive() && client) {
            if (queuing_message_count_ > kMaxClientQueuedMessage) {
                LOGW("too many message in queue, discard the message in PxServiceClient");
                return;
            }
            queuing_message_count_++;
            auto weak_self = weak_from_this();
            client->async_send(msg, [weak_self]() {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                self->queuing_message_count_--;
            });
        }
    }

}
