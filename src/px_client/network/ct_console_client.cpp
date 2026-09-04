//
// Created by RGAA on 17/05/2025.
//

#include "ct_console_client.h"
#include <atomic>
#include <type_traits>
#include <asio2/websocket/ws_client.hpp>
#include <asio2/websocket/wss_client.hpp>
#include "px_common_new/log.h"
#include "ct_client_context.h"
#include "console_client.pb.h"
#include "thunder_sdk.h"
#include "px_common_new/time_util.h"
#include "ct_auth_token.h"
#include "px_common_new/async_delay.h"
#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/reconnect_supervisor.h"
#include "px_common_new/websocket_reconnect_adapter.h"

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
            if (!exiting_ && client_) {
                return;
            }
            exiting_ = false;
            client_ = std::make_shared<ClientT>();
            const auto runtime = context_->GetMessageNotifier()->GetAsyncRuntime();
            connection_scope_ = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
            reconnect_supervisor_ = PxReconnectSupervisor::Create(runtime, MakeWebSocketReconnectOptions("client_console"));
            if (!connection_scope_ || !reconnect_supervisor_) {
                LOGE("event=module.start component=client_console code=ASYNC_WORKFLOW_CREATE_FAILED "
                     "operation=start_client outcome=failed recoverable=false");
                Exit();
                return;
            }
            client_->set_auto_reconnect(false);
            client_->keep_alive(true);
            client_->set_timeout(std::chrono::milliseconds(3000));
            if constexpr (std::is_same_v<ClientT, asio2::wss_client>) {
                client_->set_verify_mode(asio::ssl::verify_none);
            }

            auto weak_self = this->weak_from_this();
            client_->bind_init([weak_self]() {
                if (auto self = weak_self.lock(); self && !self->exiting_ && self->client_) {
                    self->client_->ws_stream().binary(true);
                    self->client_->set_no_delay(true);

                    // Generate a fresh token for every connection attempt (including auto reconnect).
                    // The token has a short lifetime (60s), so reusing the original path on reconnect
                    // would cause the Console token filter to reject the connection.
                    auto token = GenerateConnectionToken(self->appkey_);
                    const auto endpoint = self->use_legacy_cms_path_.load()
                        ? "/cms/client"
                        : "/console/client";
                    auto path = std::format(
                        "{}?appkey={}&token={}&ts={}&nonce={}&device_id={}&remote_device_id={}&remote_device_ip={}",
                        endpoint, self->appkey_, token.token, token.ts, token.nonce, self->device_id_,
                        self->remote_device_id_, self->remote_device_ip_);
                    self->client_->set_upgrade_target(path);
                }

            })
            .bind_connect([weak_self]() {
                if (asio2::get_last_error()) {
                    LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
                    if (auto self = weak_self.lock(); self && !self->exiting_
                        && self->reconnect_supervisor_) {
                        static_cast<void>(self->reconnect_supervisor_->FailActive(MakePxAsyncError(
                            PxAsyncErrorCode::kServiceNotConnected,
                            "console.connect",
                            asio2::last_error_msg(),
                            true)));
                    }
                } else {
                    if (auto self = weak_self.lock(); self && !self->exiting_ && self->client_) {
                        LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
                    }
                }

            })
            .bind_upgrade([weak_self]() {
                if (asio2::get_last_error()) {
                    LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
                    if (auto self = weak_self.lock(); self && !self->use_legacy_cms_path_.exchange(true)) {
                        LOGW("Console route unavailable; falling back to legacy /cms/client");
                    }
                    if (auto self = weak_self.lock(); self && !self->exiting_
                        && self->reconnect_supervisor_) {
                        static_cast<void>(self->reconnect_supervisor_->FailActive(MakePxAsyncError(
                            PxAsyncErrorCode::kProtocolError,
                            "console.upgrade",
                            asio2::last_error_msg(),
                            true)));
                    }
                    return;
                }
                if (auto self = weak_self.lock(); self && !self->exiting_
                    && self->reconnect_supervisor_) {
                    static_cast<void>(self->reconnect_supervisor_->MarkReady());
                }
            })
            .bind_disconnect([weak_self]() {
                if (auto self = weak_self.lock(); self && !self->exiting_) {
                    LOGE("*** Disconnected for console-client: {}", self->device_id_);
                    if (self->reconnect_supervisor_) {
                        static_cast<void>(self->reconnect_supervisor_->MarkDisconnected(MakePxAsyncError(
                            PxAsyncErrorCode::kServiceNotConnected,
                            "console.disconnect",
                            "Console websocket disconnected",
                            true)));
                    }
                }
            })
            .bind_recv([weak_self](std::string_view data) {
                if (auto self = weak_self.lock(); self && !self->exiting_) {
                    auto msg = std::string(data.data(), data.size());
                    self->ParseMessage(msg);
                }
            });

            LOGI("will connect => {}:{}/console/client, ssl: {}", host_, port_, std::is_same_v<ClientT, asio2::wss_client>);
            PxReconnectSupervisorHooks hooks{
                .start_attempt = [client = client_, host = host_, port = port_](std::uint64_t) {
                    return StartWebSocketAdapter(client, host, port, "client-console.start");
                },
                .stop_attempt = [client = client_](const std::chrono::steady_clock::time_point deadline) {
                    return StopWebSocketAdapter(client, deadline, "client-console.retry-reset");
                },
                .on_ready = [weak_self](std::uint64_t) {
                    if (const auto self = weak_self.lock(); self && !self->exiting_) {
                        self->Hello();
                    }
                },
            };
            if (!connection_scope_->Spawn("client-console-reconnect", [supervisor = reconnect_supervisor_, hooks = std::move(hooks)]() mutable {
                    return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
                })
                || !connection_scope_->Spawn("client-console-heartbeat", [weak_self] {
                    return RunHeartbeatLoop(weak_self);
                })) {
                LOGE("event=module.start component=client_console code=ASYNC_SCOPE_SPAWN_FAILED "
                     "operation=start_workflows outcome=failed recoverable=false");
                Exit();
            }
        }

        void Exit() override {
            exiting_ = true;
            if (reconnect_supervisor_) {
                reconnect_supervisor_->Stop();
            }
            if (client_) {
                static_cast<void>(RequestAsioClientStop(client_, "client-console.stop"));
            }
            if (connection_scope_) {
                connection_scope_->BeginStop();
                if (connection_scope_->IsScopeThread()) {
                    return;
                }
                static_cast<void>(connection_scope_->WaitFor(std::chrono::seconds(5)));
            }
            static_cast<void>(RequestAsioClientStop(client_, "client-console.stop-confirm"));
            static_cast<void>(WaitForAsioClientStoppedBlocking(
                client_, std::chrono::steady_clock::now() + std::chrono::seconds(3)));
            client_.reset();
            reconnect_supervisor_.reset();
            connection_scope_.reset();
        }

    private:
        bool IsAlive() const {
            return client_ && client_->is_started()
                && reconnect_supervisor_ && reconnect_supervisor_->IsReady();
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
            auto client = client_;
            if (!client) {
                return;
            }
            console_client::ConsoleClientMessage msg;
            msg.set_msg_type(console_client::ConsoleClientMessageType::kConsoleClientHello);
            msg.set_device_id(device_id_);
            const auto sub = msg.mutable_hello();
            sub->set_device_id(device_id_);
            client->async_send(msg.SerializeAsString());
        }

        void Heartbeat() {
            if (!IsAlive()) {
                return;
            }
            auto client = client_;
            if (!client) {
                return;
            }
            auto sdk_last_hb_ts = sdk_->GetLastHeartbeatTimestamp();
            bool alive = (TimeUtil::GetCurrentTimestamp() - sdk_last_hb_ts) < 10'000;
            console_client::ConsoleClientMessage msg;
            msg.set_msg_type(console_client::ConsoleClientMessageType::kConsoleClientHeartBeat);
            msg.set_device_id(device_id_);
            const auto sub = msg.mutable_heartbeat();
            sub->set_hb_index(hb_index_++);
            sub->set_connection_alive(alive);
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
