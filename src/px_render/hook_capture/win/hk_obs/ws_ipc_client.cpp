//
// Created by RGAA on 2024/3/17.
//

#include "ws_ipc_client.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <format>
#include <type_traits>
#include "px_common_new/async_mailbox.h"
#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/connection_attempt_workflow.h"
#include "px_common_new/reconnect_backoff.h"
#include "px_common_new/log.h"
#include "px_capture_new/capture_message.h"

namespace px
{

    namespace {

        constexpr auto kIpcConnectionTimeout = std::chrono::seconds(10);
        constexpr std::size_t kIpcMessageCapacity = 1024;
        const PxReconnectBackoffOptions kIpcReconnectOptions{
            .initial_delay = std::chrono::milliseconds(100),
            .maximum_delay = std::chrono::seconds(5),
            .multiplier = 2.0,
            .jitter_ratio = 0.2,
        };

        template<typename T>
        T DecodeIpcValue(std::string_view bytes) {
            static_assert(std::is_trivially_copyable_v<T>);
            std::array<char, sizeof(T)> storage{};
            std::copy_n(bytes.begin(), storage.size(), storage.begin());
            return std::bit_cast<T>(storage);
        }

    }

    std::shared_ptr<WsIpcClient> WsIpcClient::Make(int port) {
        return std::make_shared<WsIpcClient>(port);
    }

    WsIpcClient::WsIpcClient(int port) : port_(port) {}

    WsIpcClient::~WsIpcClient() {
        Exit();
    }

    void WsIpcClient::Start() {
        if (started_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        exiting_.store(false, std::memory_order_release);
        async_runtime_ = PxAsyncRuntime::Create({.worker_threads = 1});
        if (!async_runtime_ || !async_runtime_->Start()) {
            LOGE("ws ipc client failed to start async runtime");
            exiting_.store(true, std::memory_order_release);
            return;
        }
        async_scope_ = PxAsyncScope::Create(async_runtime_, PxAsyncLane::kState);
        connection_workflow_ = PxConnectionAttemptWorkflow::Create(async_runtime_, kIpcConnectionTimeout);
        connection_backoff_ = PxReconnectBackoff::Create(kIpcReconnectOptions);
        if (!async_scope_ || !connection_workflow_ || !connection_backoff_) {
            LOGE("ws ipc client failed to create async workflow");
            Exit();
            return;
        }
        incoming_messages_ = PxAsyncMailbox<std::string>::Create(async_scope_->Executor(), kIpcMessageCapacity);
        if (!incoming_messages_) {
            LOGE("ws ipc client failed to create async workflow");
            Exit();
            return;
        }

        const auto weak_self = weak_from_this();
        if (!async_scope_->Spawn("ipc-receive-loop", [weak_self, mailbox = incoming_messages_]() {
                return RunIncomingMessageLoop(weak_self, mailbox);
            })) {
            LOGE("ws ipc client failed to start receive coroutine");
            Exit();
            return;
        }

        // Host net_ws listens with asio2::http_server (plain WS). Using wss_client here
        // previously made the injected DLL unable to connect to /ipc.
        ws_client_ = std::make_shared<asio2::ws_client>();
        ws_client_->set_auto_reconnect(false);
        ws_client_->set_timeout(std::chrono::seconds(2));
        ws_client_->bind_init([weak_self]() {
            const auto self = weak_self.lock();
            if (!self || self->exiting_.load(std::memory_order_acquire) || !self->ws_client_ || !self->connection_workflow_) {
                return;
            }
            self->ws_client_->ws_stream().binary(true);
            self->ws_client_->set_no_delay(true);
        })
        .bind_connect([weak_self]() {
            if (asio2::get_last_error()) {
                LOGE("event=transport.connection_attempt component=obs_ipc code={} operation=connect outcome=failure "
                     "recoverable=true reason={}", asio2::last_error_val(), asio2::last_error_msg());
                if (const auto self = weak_self.lock(); self && self->connection_workflow_) {
                    static_cast<void>(self->connection_workflow_->FailActive(
                        self->connection_generation_.load(std::memory_order_acquire),
                        MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "ipc.connect", asio2::last_error_msg(), true)));
                }
            } else {
                if (const auto self = weak_self.lock()) {
                    LOGI("ws ipc client connected to 127.0.0.1:{}/ipc", self->port_);
                }
            }
        })
        .bind_upgrade([weak_self]() {
            if (asio2::get_last_error()) {
                LOGE("event=transport.connection_attempt component=obs_ipc code={} operation=upgrade outcome=failure "
                     "recoverable=true reason={}", asio2::last_error_val(), asio2::last_error_msg());
                if (const auto self = weak_self.lock(); self && self->connection_workflow_) {
                    static_cast<void>(self->connection_workflow_->FailActive(
                        self->connection_generation_.load(std::memory_order_acquire),
                        MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "ipc.upgrade", asio2::last_error_msg(), true)));
                }
            } else {
                if (const auto self = weak_self.lock(); self && self->connection_workflow_) {
                    static_cast<void>(
                        self->connection_workflow_->MarkReady(self->connection_generation_.load(std::memory_order_acquire)));
                }
            }
        })
        .bind_disconnect([weak_self]() {
            if (const auto self = weak_self.lock(); self && self->connection_workflow_) {
                static_cast<void>(self->connection_workflow_->MarkDisconnected(
                    self->connection_generation_.load(std::memory_order_acquire),
                    MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "ipc.disconnect", "IPC websocket disconnected", true)));
            }
        })
        .bind_recv([weak_self](std::string_view data) {
            const auto self = weak_self.lock();
            if (!self || self->exiting_.load(std::memory_order_acquire)) {
                return;
            }
            const auto mailbox = self->incoming_messages_;
            if (mailbox) {
                static_cast<void>(mailbox->TryPush(std::string(data)));
            }
        });

        // Identify ourselves by pid: the render only accepts /ipc connections from
        // pids it registered via RegisterIpcPid (wrote hook boot config for them).
        std::string ipc_path = std::format("/ipc?pid={}", GetCurrentProcessId());
        LOGI("ws ipc client starting: 127.0.0.1:{}{}", port_, ipc_path);
        if (!async_scope_->Spawn("ipc-connection-loop",
                                 [weak_self, workflow = connection_workflow_, backoff = connection_backoff_, client = ws_client_,
                                  port = port_, ipc_path = std::move(ipc_path)]() mutable {
            return RunConnectionLoop(weak_self, workflow, backoff, client, port, std::move(ipc_path));
        })) {
            LOGE("ws ipc client failed to start connection coroutine");
            Exit();
        }
    }

    void WsIpcClient::Exit() {
        if (exiting_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        if (incoming_messages_) {
            static_cast<void>(incoming_messages_->Close(
                MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "ipc.receive", "IPC websocket is stopping")));
        }
        if (connection_workflow_) {
            connection_workflow_->Stop();
        }
        const auto client = ws_client_;
        static_cast<void>(RequestAsioClientStop(client, "ipc.adapter-stop"));
        auto drained = true;
        if (async_scope_) {
            async_scope_->BeginStop();
            const auto called_from_scope = async_scope_->IsScopeThread();
            if (called_from_scope) {
                drained = false;
            } else {
                const auto adapter_stopped = WaitForAsioClientStoppedBlocking(client, deadline);
                const auto remaining = std::max(
                    std::chrono::milliseconds::zero(),
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
                drained = adapter_stopped && async_scope_->WaitFor(remaining);
            }
            if (!drained && !called_from_scope) {
                LOGE("event=async.scope_drain component=obs_ipc code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                     "operation=stop_client outcome=timeout recoverable=false outstanding={}",
                     async_scope_->GetStatistics().outstanding);
            } else if (called_from_scope) {
                LOGI("event=async.scope_drain component=obs_ipc operation=stop_client outcome=deferred "
                     "reason=shutdown_requested_from_callback outstanding={}",
                     async_scope_->GetStatistics().outstanding);
            }
        }
        if (async_runtime_) {
            async_runtime_->RequestStop();
            if (!async_runtime_->IsRuntimeThread()) {
                async_runtime_->Join();
            } else {
                drained = false;
                LOGI("event=async.runtime_stop component=obs_ipc operation=stop_client outcome=deferred "
                     "reason=shutdown_requested_from_runtime_thread");
            }
        }
        if (drained) {
            ws_client_.reset();
            incoming_messages_.reset();
            connection_workflow_.reset();
            connection_backoff_.reset();
            async_scope_.reset();
            async_runtime_.reset();
            started_.store(false, std::memory_order_release);
        }
    }

    void WsIpcClient::RegisterIpcMessageCallback(WsIpcMessageCallback&& callback) {
        std::lock_guard lock(callback_mutex_);
        ipc_cbk_ = std::move(callback);
    }

    void WsIpcClient::PostIpcMessage(const std::string& msg) {
        if (!ws_client_) {
            const auto count = null_drop_count_.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (count == 1 || (count % 200) == 0) {
                LOGE("ws ipc PostIpcMessage: client is null, drop {} bytes n={}", msg.size(), count);
            }
            return;
        }
        if (!ws_client_->is_started()) {
            const auto count = stopped_drop_count_.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (count == 1 || (count % 200) == 0) {
                LOGE("ws ipc PostIpcMessage: not started, drop {} bytes n={}", msg.size(), count);
            }
            return;
        }
        if (msg.empty()) {
            LOGE("ws ipc PostIpcMessage: empty payload");
            return;
        }
        ws_client_->async_send(msg);
    }

    PxAwaitable<void> WsIpcClient::RunIncomingMessageLoop(
        std::weak_ptr<WsIpcClient> weak_client,
        std::shared_ptr<PxAsyncMailbox<std::string>> mailbox) {
        for (;;) {
            auto message = co_await PxAsyncMailbox<std::string>::ReceiveUntil(mailbox, std::chrono::steady_clock::time_point::max());
            if (!message) {
                co_return;
            }
            const auto self = weak_client.lock();
            if (!self || self->exiting_.load(std::memory_order_acquire)) {
                co_return;
            }
            self->DispatchIpcMessage(message.Value());
        }
    }

    PxAwaitable<void> WsIpcClient::RunConnectionLoop(
        std::weak_ptr<WsIpcClient> weak_client,
        std::shared_ptr<PxConnectionAttemptWorkflow> workflow,
        std::shared_ptr<PxReconnectBackoff> backoff,
        std::shared_ptr<asio2::ws_client> client,
        int port,
        std::string path) {
        for (;;) {
            auto self = weak_client.lock();
            if (!self || self->exiting_.load(std::memory_order_acquire)) {
                co_return;
            }
            const auto attempt = workflow->StartAttempt();
            if (!attempt) {
                co_return;
            }
            const auto ticket = attempt.Value();
            self->connection_generation_.store(ticket.generation, std::memory_order_release);
            self.reset();

            LOGI("event=transport.connection_attempt component=obs_ipc generation={}", ticket.generation);
            if (!client->async_start("127.0.0.1", port, path)) {
                static_cast<void>(workflow->FailActive(
                    ticket.generation, MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "ipc.start", asio2::last_error_msg(), true)));
            }

            const auto ready = co_await PxConnectionAttemptWorkflow::WaitUntilReady(
                workflow, ticket, std::chrono::steady_clock::now() + kIpcConnectionTimeout);
            if (ready) {
                backoff->Reset();
                LOGI("event=transport.connection_ready component=obs_ipc generation={}", ticket.generation);
                const auto disconnected = co_await PxConnectionAttemptWorkflow::WaitUntilDisconnected(workflow, ticket);
                if (!disconnected) {
                    if (disconnected.Error().code == PxAsyncErrorCode::kServiceStopped ||
                        disconnected.Error().code == PxAsyncErrorCode::kCancelled) {
                        co_return;
                    }
                    LOGW("event=transport.connection_lost component=obs_ipc generation={} stage={} code={} recoverable={}",
                         ticket.generation, disconnected.Error().stage, disconnected.Error().StableCode(),
                         disconnected.Error().retryable);
                } else {
                    const auto& reason = disconnected.Value().reason;
                    LOGW("event=transport.connection_lost component=obs_ipc generation={} stage={} code={} recoverable={}",
                         ticket.generation, reason.stage, reason.StableCode(), reason.retryable);
                }
            } else if (ready.Error().code == PxAsyncErrorCode::kServiceStopped || ready.Error().code == PxAsyncErrorCode::kCancelled) {
                co_return;
            } else {
                LOGW("event=transport.connection_lost component=obs_ipc generation={} stage={} code={} recoverable={}",
                     ticket.generation, ready.Error().stage, ready.Error().StableCode(), ready.Error().retryable);
            }

            const auto step = backoff->Next();
            LOGI("event=transport.reconnect_wait component=obs_ipc generation={} attempt={} delay_ms={}",
                 ticket.generation, step.attempt, step.delay.count());
            const auto waited = co_await PxReconnectBackoff::Wait(step.delay);
            if (!waited) {
                co_return;
            }
        }
    }

    void WsIpcClient::DispatchIpcMessage(const std::string& msg) {
        WsIpcMessageCallback callback;
        {
            std::lock_guard lock(callback_mutex_);
            callback = ipc_cbk_;
        }
        if (!callback || msg.size() < sizeof(CaptureBaseMessage)) {
            return;
        }
        const auto base_message = DecodeIpcValue<CaptureBaseMessage>(msg);
        if (base_message.type_ == kMouseEventMessage) {
            if (msg.size() != sizeof(MouseEventMessage)) {
                LOGE("msg size != sizeof(MouseEventMessage), msg size: {}, event size: {}", msg.size(), sizeof(MouseEventMessage));
                return;
            }
            callback(std::make_shared<MouseEventMessage>(DecodeIpcValue<MouseEventMessage>(msg)));
        }
        else if (base_message.type_ == kKeyboardEventMessage) {
            if (msg.size() != sizeof(KeyboardEventMessage)) {
                LOGE("msg size != sizeof(KeyboardEventMessage), msg size: {}, event size: {}", msg.size(), sizeof(KeyboardEventMessage));
                return;
            }
            callback(std::make_shared<KeyboardEventMessage>(DecodeIpcValue<KeyboardEventMessage>(msg)));
        }
        else if (base_message.type_ == kCaptureResetInputMessage) {
            if (msg.size() != sizeof(CaptureResetInputMessage)) {
                LOGE("msg size != sizeof(CaptureResetInputMessage), msg size: {}", msg.size());
                return;
            }
            callback(std::make_shared<CaptureResetInputMessage>(DecodeIpcValue<CaptureResetInputMessage>(msg)));
        }
    }

}
