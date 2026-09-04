//
// Created by RGAA on 8/12/2024.
//

#include "wss_connection.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include <asio2/asio2.hpp>
#include <asio2/websocket/wss_client.hpp>

#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/reconnect_supervisor.h"
#include "px_common_new/ws_control_signal.h"
#include "sdk_messages.h"
#include "sdk_websocket_reconnect.h"

namespace px {

WssConnection::WssConnection(
    const std::shared_ptr<ThunderSdkParams>& params,
    const std::shared_ptr<MessageNotifier>& notifier,
    const std::string& host,
    const int port,
    const std::string& path)
    : Connection(params, notifier), host_(host), port_(port), path_(path) {}

WssConnection::~WssConnection() {
    Stop();
}

void WssConnection::Start() {
    if (started_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    exiting_.store(false, std::memory_order_release);
    terminal_rejection_.store(false, std::memory_order_release);
    const auto runtime = msg_notifier_ ? msg_notifier_->GetAsyncRuntime() : std::shared_ptr<PxAsyncRuntime>{};
    if (!runtime || runtime->IsStopping()) {
        LOGE("event=module.start component=sdk_wss code=ASYNC_RUNTIME_UNAVAILABLE "
             "operation=start_client outcome=failed recoverable=false");
        started_.store(false, std::memory_order_release);
        exiting_.store(true, std::memory_order_release);
        return;
    }

    async_scope_ = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    reconnect_supervisor_ = PxReconnectSupervisor::Create(
        runtime, MakeWebSocketReconnectOptions("sdk_wss"));
    client_ = std::make_shared<asio2::wss_client>();
    if (!async_scope_ || !reconnect_supervisor_ || !client_) {
        LOGE("event=module.start component=sdk_wss code=ASYNC_WORKFLOW_CREATE_FAILED "
             "operation=start_client outcome=failed recoverable=false");
        Stop();
        return;
    }

    client_->set_auto_reconnect(false);
    client_->set_timeout(std::chrono::milliseconds(2000));
    const auto weak_self = weak_from_this();
    client_->bind_init([weak_self]() {
        const auto self = weak_self.lock();
        if (!self || self->exiting_.load(std::memory_order_acquire) || !self->client_) {
            return;
        }
        self->client_->set_no_delay(true);
        self->client_->ws_stream().set_option(
            websocket::stream_base::decorator([](websocket::request_type& request) {
                request.set(http::field::authorization, "websocket-client-authorization");
            }));
    }).bind_connect([weak_self]() {
        const auto self = weak_self.lock();
        if (!self || self->exiting_.load(std::memory_order_acquire) || !self->reconnect_supervisor_) {
            return;
        }
        if (asio2::get_last_error()) {
            LOGW("event=transport.connection_attempt component=sdk_wss code={} operation=connect "
                 "outcome=failed recoverable=true reason={}",
                 asio2::last_error_val(), asio2::last_error_msg());
            static_cast<void>(self->reconnect_supervisor_->FailActive(MakePxAsyncError(
                PxAsyncErrorCode::kServiceNotConnected,
                "sdk-wss.connect",
                asio2::last_error_msg(),
                true)));
            return;
        }
        LOGI("event=transport.tcp_connected component=sdk_wss local_address={} local_port={}",
             self->client_->local_address(), self->client_->local_port());
    }).bind_disconnect([weak_self]() {
        const auto self = weak_self.lock();
        if (!self || self->exiting_.load(std::memory_order_acquire) || !self->reconnect_supervisor_) {
            return;
        }
        static_cast<void>(self->reconnect_supervisor_->MarkDisconnected(MakePxAsyncError(
            PxAsyncErrorCode::kServiceNotConnected,
            "sdk-wss.disconnect",
            "SDK secure websocket disconnected",
            true)));
    }).bind_upgrade([weak_self]() {
        const auto self = weak_self.lock();
        if (!self || self->exiting_.load(std::memory_order_acquire) || !self->reconnect_supervisor_) {
            return;
        }
        if (asio2::get_last_error()) {
            LOGW("event=transport.connection_attempt component=sdk_wss code={} operation=upgrade "
                 "outcome=failed recoverable=true reason={}",
                 asio2::last_error_val(), asio2::last_error_msg());
            static_cast<void>(self->reconnect_supervisor_->FailActive(MakePxAsyncError(
                PxAsyncErrorCode::kProtocolError,
                "sdk-wss.upgrade",
                asio2::last_error_msg(),
                true)));
            return;
        }
        static_cast<void>(self->reconnect_supervisor_->MarkReady());
    }).bind_recv([weak_self](std::string_view data) {
        const auto self = weak_self.lock();
        if (!self || self->exiting_.load(std::memory_order_acquire)
            || self->terminal_rejection_.load(std::memory_order_acquire)) {
            return;
        }
        const auto rejection = ParseWsControlRejection(data);
        if (rejection != WsControlRejection::kNone) {
            self->terminal_rejection_.store(true, std::memory_order_release);
            LOGW("event=transport.connection_terminal component=sdk_wss code=SDK_WEBSOCKET_SESSION_REJECTED "
                 "operation=receive outcome=rejected recoverable=false reason={}",
                 static_cast<int>(rejection));
            if (self->msg_notifier_) {
                self->msg_notifier_->SendAppMessage(SdkMsgWsConnectionRejected{.rejection_ = rejection});
            }
            if (self->reconnect_supervisor_) {
                static_cast<void>(self->reconnect_supervisor_->MarkDisconnected(
                    MakeSdkWebSocketRejectionError(rejection)));
            }
            return;
        }
        if (self->msg_cbk_) {
            self->msg_cbk_(Data::From(std::string(data)));
        }
    });

    PxReconnectSupervisorHooks hooks{
        .start_attempt = [client = client_, host = host_, port = port_, path = path_](std::uint64_t) {
            return StartWebSocketAdapter(client, host, port, path, "sdk-wss.start");
        },
        .stop_attempt = [client = client_](const std::chrono::steady_clock::time_point deadline) {
            return StopWebSocketAdapter(client, deadline, "sdk-wss.retry-reset");
        },
        .on_ready = [weak_self](std::uint64_t) {
            if (const auto self = weak_self.lock(); self && !self->exiting_.load(std::memory_order_acquire) && self->conn_cbk_) {
                self->conn_cbk_();
            }
        },
        .on_lost = [weak_self](std::uint64_t, const PxAsyncError&, const bool was_ready) {
            if (const auto self = weak_self.lock(); self && was_ready && !self->terminal_rejection_.load(std::memory_order_acquire)
                && !self->exiting_.load(std::memory_order_acquire) && self->dis_conn_cbk_) {
                self->dis_conn_cbk_();
            }
        },
    };
    if (!async_scope_->Spawn("sdk-wss-reconnect", [supervisor = reconnect_supervisor_, hooks = std::move(hooks)]() mutable {
            return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
        })) {
        LOGE("event=module.start component=sdk_wss code=ASYNC_SCOPE_SPAWN_FAILED "
             "operation=start_reconnect outcome=failed recoverable=false");
        Stop();
    }
}

std::shared_ptr<PxAsyncScope> WssConnection::BeginStop() {
    if (exiting_.exchange(true, std::memory_order_acq_rel)) {
        return async_scope_;
    }
    if (reconnect_supervisor_) {
        reconnect_supervisor_->Stop();
    }
    static_cast<void>(RequestAsioClientStop(client_, "sdk-wss.adapter-stop"));
    if (async_scope_) {
        async_scope_->BeginStop();
    }
    return async_scope_;
}

void WssConnection::FinishStop() {
    std::lock_guard lock(stop_mutex_);
    if (async_scope_ && async_scope_->GetStatistics().outstanding != 0) {
        return;
    }
    client_.reset();
    reconnect_supervisor_.reset();
    async_scope_.reset();
    queuing_message_count_.store(0, std::memory_order_release);
    started_.store(false, std::memory_order_release);
    deferred_stop_scheduled_.store(false, std::memory_order_release);
}

void WssConnection::ScheduleDeferredStop() {
    if (deferred_stop_scheduled_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const auto weak_self = weak_from_this();
    asio::post(asio::system_executor{}, [weak_self] {
        if (const auto self = weak_self.lock()) {
            self->Stop();
        }
    });
}

void WssConnection::Stop() {
    Connection::Stop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto scope = BeginStop();
    if (scope && scope->IsScopeThread()) {
        LOGI("event=async.scope_drain component=sdk_wss operation=stop_client outcome=deferred "
             "reason=shutdown_requested_from_runtime_thread outstanding={}",
             scope->GetStatistics().outstanding);
        ScheduleDeferredStop();
        return;
    }
    const auto remaining = std::max(
        std::chrono::milliseconds::zero(),
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
    const auto scope_drained = !scope || scope->WaitFor(remaining);
    static_cast<void>(RequestAsioClientStop(client_, "sdk-wss.adapter-stop-confirm"));
    const auto adapter_stopped = WaitForAsioClientStoppedBlocking(client_, deadline);
    if (!scope_drained || !adapter_stopped) {
        LOGE("event=async.scope_drain component=sdk_wss code=ASYNC_SCOPE_DRAIN_TIMEOUT "
             "operation=stop_client outcome=timeout recoverable=false outstanding={}",
             scope ? scope->GetStatistics().outstanding : 0);
        return;
    }
    FinishStop();
}

void WssConnection::PostBinaryMessage(std::shared_ptr<Data> msg) {
    if (!msg) {
        LOGW("event=transport.message_rejected component=sdk_wss code=INVALID_PAYLOAD "
             "operation=send_binary outcome=rejected recoverable=false");
        return;
    }
    if (client_ && client_->is_started() && reconnect_supervisor_ && reconnect_supervisor_->IsReady()) {
        client_->ws_stream().binary(true);
        ++queuing_message_count_;
        const auto weak_self = weak_from_this();
        client_->async_send(msg->AsString(), [weak_self]() {
            if (const auto self = weak_self.lock()) {
                const auto remaining = --self->queuing_message_count_;
                if (remaining <= kFileTransferQueueLowWatermark) {
                    self->NotifyFileTransferWritable();
                }
            }
        });
    }
}

void WssConnection::PostTextMessage(const std::string& msg) {
    if (client_ && client_->is_started() && reconnect_supervisor_ && reconnect_supervisor_->IsReady()) {
        client_->ws_stream().text(true);
        ++queuing_message_count_;
        const auto weak_self = weak_from_this();
        client_->async_send(msg, [weak_self]() {
            if (const auto self = weak_self.lock()) {
                const auto remaining = --self->queuing_message_count_;
                if (remaining <= kFileTransferQueueLowWatermark) {
                    self->NotifyFileTransferWritable();
                }
            }
        });
    }
}

bool WssConnection::IsAlive() {
    return !exiting_.load(std::memory_order_acquire) && client_ && client_->is_started()
        && reconnect_supervisor_ && reconnect_supervisor_->IsReady();
}

std::uint64_t WssConnection::ConnectionGeneration() const {
    return reconnect_supervisor_ ? reconnect_supervisor_->Generation() : 0;
}

} // namespace px
