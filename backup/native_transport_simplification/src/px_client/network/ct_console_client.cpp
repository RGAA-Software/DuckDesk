//
// Created by RGAA on 17/05/2025.
//

#include "ct_console_client.h"
#include <atomic>
#include <mutex>
#include <type_traits>
#include <asio2/websocket/ws_client.hpp>
#include <asio2/websocket/wss_client.hpp>
#include "px_common/log.h"
#include "ct_client_context.h"
#include "console_client.pb.h"
#include "thunder_sdk.h"
#include "px_common/time_util.h"
#include "ct_auth_token.h"
#include "px_common/async_delay.h"
#include "px_common/asio_client_shutdown.h"
#include "px_common/message_notifier.h"
#include "px_common/reconnect_supervisor.h"
#include "px_common/websocket_reconnect_adapter.h"

namespace px
{
    template<typename ClientT>
    class CtConsoleClientImpl : public CtConsoleClient,
                            public std::enable_shared_from_this<CtConsoleClientImpl<ClientT>> {
    public:
        explicit CtConsoleClientImpl(const std::shared_ptr<ThunderSdk>& sdk,
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

        ~CtConsoleClientImpl() override {
            Exit();
        }

        void Start() override {
            std::unique_lock operation_lock(operation_mutex_);
            {
                std::lock_guard lock(network_mutex_);
                if (client_) {
                    return;
                }
            }
            exiting_ = false;
            deferred_exit_scheduled_ = false;
            const auto runtime = context_->GetMessageNotifier()->GetAsyncRuntime();
            const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
            const auto supervisor = PxReconnectSupervisor::Create(runtime, MakeWebSocketReconnectOptions("client_console"));
            if (!scope || !supervisor) {
                LOGE("event=module.start component=client_console code=ASYNC_WORKFLOW_CREATE_FAILED "
                     "operation=start_client outcome=failed recoverable=false");
                operation_lock.unlock();
                Exit();
                return;
            }
            {
                std::lock_guard lock(network_mutex_);
                async_runtime_ = runtime;
                connection_scope_ = scope;
                reconnect_supervisor_ = supervisor;
            }
            auto weak_self = this->weak_from_this();
            LOGI("will connect => {}:{}/console/client, ssl: {}", host_, port_, std::is_same_v<ClientT, asio2::wss_client>);
            PxReconnectSupervisorHooks hooks{
                .start_attempt = [weak_self, supervisor, host = host_, port = port_](const std::uint64_t generation) {
                    const auto self = weak_self.lock();
                    if (!self || self->exiting_) {
                        return PxResult<void>::Failure(MakePxAsyncError(
                            PxAsyncErrorCode::kServiceStopped, "client-console.start", "Client Console owner is stopping"));
                    }
                    const auto client = std::make_shared<ClientT>();
                    const auto weak_client = std::weak_ptr<ClientT>(client);
                    client->set_auto_reconnect(false);
                    client->keep_alive(true);
                    client->set_timeout(std::chrono::milliseconds(3000));
                    if constexpr (std::is_same_v<ClientT, asio2::wss_client>) {
                        client->set_verify_mode(asio::ssl::verify_none);
                    }
                    client->bind_init([weak_self, weak_client]() {
                        const auto owner = weak_self.lock();
                        const auto current = weak_client.lock();
                        if (!owner || !current || owner->exiting_) {
                            return;
                        }
                        current->ws_stream().binary(true);
                        current->set_no_delay(true);
                        const auto token = GenerateConnectionToken(owner->appkey_);
                        const auto endpoint = owner->use_legacy_cms_path_.load() ? "/cms/client" : "/console/client";
                        current->set_upgrade_target(std::format(
                            "{}?appkey={}&token={}&ts={}&nonce={}&device_id={}&remote_device_id={}&remote_device_ip={}",
                            endpoint, owner->appkey_, token.token, token.ts, token.nonce, owner->device_id_,
                            owner->remote_device_id_, owner->remote_device_ip_));
                    }).bind_connect([weak_self, weak_client, supervisor, generation]() {
                        const auto owner = weak_self.lock();
                        const auto current = weak_client.lock();
                        if (!owner || !current || owner->exiting_) {
                            return;
                        }
                        if (asio2::get_last_error()) {
                            static_cast<void>(supervisor->FailActive(generation, MakePxAsyncError(
                                PxAsyncErrorCode::kServiceNotConnected, "console.connect", asio2::last_error_msg(), true)));
                            return;
                        }
                        LOGI("connect success : {} {} ", current->local_address().c_str(), current->local_port());
                    }).bind_upgrade([weak_self, supervisor, generation]() {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_) {
                            if (asio2::get_last_error()) {
                                if (!owner->use_legacy_cms_path_.exchange(true)) {
                                    LOGW("event=transport.route_fallback component=client_console code=PRIMARY_ROUTE_UNAVAILABLE "
                                         "operation=upgrade outcome=fallback recoverable=true target=/cms/client");
                                }
                                static_cast<void>(supervisor->FailActive(generation, MakePxAsyncError(
                                    PxAsyncErrorCode::kProtocolError, "console.upgrade", asio2::last_error_msg(), true)));
                                return;
                            }
                            static_cast<void>(supervisor->MarkReady(generation));
                        }
                    }).bind_disconnect([weak_self, supervisor, generation]() {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_) {
                            static_cast<void>(supervisor->MarkDisconnected(generation, MakePxAsyncError(
                                PxAsyncErrorCode::kServiceNotConnected, "console.disconnect", "Console websocket disconnected", true)));
                        }
                    }).bind_recv([weak_self](std::string_view data) {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_) {
                            owner->ParseMessage(std::string(data));
                        }
                    });
                    {
                        std::lock_guard lock(self->network_mutex_);
                        self->client_ = client;
                    }
                    return StartWebSocketAdapter(client, host, port, "client-console.start");
                },
                .stop_attempt = [weak_self](const std::chrono::steady_clock::time_point deadline) -> PxAwaitable<PxResult<void>> {
                    const auto self = weak_self.lock();
                    if (!self) {
                        co_return PxResult<void>::Success();
                    }
                    co_return co_await StopWebSocketAdapter(self->ClientSnapshot(), deadline, "client-console.retry-reset");
                },
                .on_ready = [weak_self](std::uint64_t) {
                    if (const auto self = weak_self.lock(); self && !self->exiting_) {
                        self->Hello();
                    }
                },
            };
            if (!scope->Spawn("client-console-reconnect", [supervisor, hooks = std::move(hooks)]() mutable {
                    return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
                })
                || !scope->Spawn("client-console-heartbeat", [weak_self] {
                    return RunHeartbeatLoop(weak_self);
                })) {
                LOGE("event=module.start component=client_console code=ASYNC_SCOPE_SPAWN_FAILED "
                     "operation=start_workflows outcome=failed recoverable=false");
                operation_lock.unlock();
                Exit();
            }
        }

        void Exit() override {
            std::unique_lock operation_lock(operation_mutex_);
            exiting_ = true;
            std::shared_ptr<ClientT> client;
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
            const auto result = StopWebSocketConnectionBlocking(client, scope, std::chrono::seconds(5), "client-console.stop");
            if (result.deferred) {
                ScheduleDeferredExit();
                return;
            }
            if (!result.Succeeded()) {
                LOGE("event=async.scope_drain component=client_console code=ASYNC_SCOPE_DRAIN_TIMEOUT operation=exit "
                     "outcome=timeout recoverable=false scope_drained={} adapter_stopped={} outstanding={}",
                     result.scope_drained, result.adapter_stopped, scope ? scope->GetStatistics().outstanding : 0);
                return;
            }
            std::lock_guard lock(network_mutex_);
            client_.reset();
            reconnect_supervisor_.reset();
            connection_scope_.reset();
            async_runtime_.reset();
            deferred_exit_scheduled_ = false;
        }

    private:
        void ScheduleDeferredExit() {
            if (deferred_exit_scheduled_.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            std::shared_ptr<PxAsyncRuntime> runtime;
            {
                std::lock_guard lock(network_mutex_);
                runtime = async_runtime_;
            }
            const auto weak_self = this->weak_from_this();
            if (!runtime || !runtime->DeferBlocking([weak_self] {
                    if (const auto self = weak_self.lock()) {
                        self->Exit();
                    }
                })) {
                deferred_exit_scheduled_ = false;
                LOGE("event=async.scope_drain component=client_console code=ASYNC_DEFER_FAILED operation=exit "
                     "outcome=failed recoverable=false");
            }
        }

        [[nodiscard]] std::shared_ptr<ClientT> ClientSnapshot() const {
            std::lock_guard lock(network_mutex_);
            return client_;
        }

        bool IsAlive() const {
            std::lock_guard lock(network_mutex_);
            return !exiting_ && client_ && client_->is_started() && reconnect_supervisor_ && reconnect_supervisor_->IsReady();
        }

        static PxAwaitable<void> RunHeartbeatLoop(std::weak_ptr<CtConsoleClientImpl<ClientT>> weak_client) {
            for (;;) {
                const auto waited = co_await WaitForAsyncDelay(std::chrono::seconds(1), "client-console.heartbeat");
                const auto self = weak_client.lock();
                if (!waited || !self || self->exiting_) {
                    co_return;
                }
                self->Heartbeat();
            }
        }

        void Hello() {
            if (!IsAlive()) {
                return;
            }
            const auto client = ClientSnapshot();
            if (!client) {
                return;
            }
            console_client::ConsoleClientMessage msg;
            msg.set_msg_type(console_client::ConsoleClientMessageType::kConsoleClientHello);
            msg.set_device_id(device_id_);
            auto& sub = *msg.mutable_hello();
            sub.set_device_id(device_id_);
            client->async_send(msg.SerializeAsString());
        }

        void Heartbeat() {
            if (!IsAlive()) {
                return;
            }
            const auto client = ClientSnapshot();
            if (!client) {
                return;
            }
            auto sdk_last_hb_ts = sdk_->GetLastHeartbeatTimestamp();
            bool alive = (TimeUtil::GetCurrentTimestamp() - sdk_last_hb_ts) < 10'000;
            console_client::ConsoleClientMessage msg;
            msg.set_msg_type(console_client::ConsoleClientMessageType::kConsoleClientHeartBeat);
            msg.set_device_id(device_id_);
            auto& sub = *msg.mutable_heartbeat();
            sub.set_hb_index(hb_index_++);
            sub.set_connection_alive(alive);
            client->async_send(msg.SerializeAsString());
        }

        void ParseMessage(const std::string& data) {
            auto msg = std::make_shared<console_client::ConsoleClientMessage>();
            if (!msg->ParseFromArray(data.data(), data.size())) {
                LOGE("CtConsoleClient parse message failed!");
                return;
            }
            if (msg->msg_type() == console_client::ConsoleClientMessageType::kConsoleClientHeartBeat) {
                LOGI("Heartbeat: {}", msg->device_id(), msg->heartbeat().hb_index());
            }
        }

    private:
        std::shared_ptr<ThunderSdk> sdk_;
        std::shared_ptr<ClientContext> context_ = nullptr;
        std::shared_ptr<PxAsyncRuntime> async_runtime_{};
        std::shared_ptr<ClientT> client_ = nullptr;
        std::shared_ptr<PxAsyncScope> connection_scope_{};
        std::shared_ptr<PxReconnectSupervisor> reconnect_supervisor_{};
        std::string host_;
        int port_ = 0;
        std::string device_id_;
        std::string remote_device_id_;
        std::string remote_device_ip_;
        std::string appkey_;
        int64_t hb_index_ = 0;
        std::atomic_bool exiting_ = false;
        std::atomic_bool use_legacy_cms_path_ = false;
        std::atomic_bool deferred_exit_scheduled_{false};
        mutable std::mutex network_mutex_{};
        std::mutex operation_mutex_{};
    };

    std::shared_ptr<CtConsoleClient> CtConsoleClient::Make(const std::shared_ptr<ThunderSdk>& sdk,
                                                   const std::shared_ptr<ClientContext>& ctx,
                                                   const std::string& host,
                                                   int port,
                                                   const std::string& device_id,
                                                   const std::string& remote_device_id,
                                                   const std::string& remote_device_ip,
                                                   const std::string& appkey,
                                                   bool /*ssl*/) {
        return std::make_shared<CtConsoleClientImpl<asio2::wss_client>>(sdk, ctx, host, port, device_id, remote_device_id, remote_device_ip, appkey);
    }

}
