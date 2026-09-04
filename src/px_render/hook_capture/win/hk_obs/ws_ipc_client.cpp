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
#include "px_common_new/async_scope_drain.h"
#include "px_common_new/reconnect_supervisor.h"
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

        PxAwaitable<void> StopIpcFromRuntime(
            const std::shared_ptr<WsIpcClient>& owner,
            const std::chrono::steady_clock::time_point deadline) {
            const auto stopped = co_await WsIpcClient::StopAsync(owner, deadline);
            if (!stopped) {
                LOGE("event=async.scope_drain component=obs_ipc code={} operation=stop_client "
                     "outcome=failed recoverable={} reason={}",
                     stopped.Error().StableCode(), stopped.Error().retryable, stopped.Error().message);
                co_return;
            }
            LOGI("event=async.scope_drain component=obs_ipc operation=stop_client outcome=success");
        }

    }

    std::shared_ptr<WsIpcClient> WsIpcClient::Make(int port) {
        return std::make_shared<WsIpcClient>(port);
    }

    WsIpcClient::WsIpcClient(int port) : port_(port) {}

    WsIpcClient::~WsIpcClient() {
        Exit();
    }

    bool WsIpcClient::IsStarted() const {
        return started_.load(std::memory_order_acquire) && !exiting_.load(std::memory_order_acquire);
    }

    bool WsIpcClient::IsConnected() const {
        return IsStarted() && ws_client_ && ws_client_->is_started() && connection_supervisor_ && connection_supervisor_->IsReady();
    }

    std::uint64_t WsIpcClient::ConnectionGeneration() const {
        return connection_supervisor_ ? connection_supervisor_->Generation() : 0;
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
            async_runtime_.reset();
            started_.store(false, std::memory_order_release);
            return;
        }
        async_scope_ = PxAsyncScope::Create(async_runtime_, PxAsyncLane::kState);
        connection_supervisor_ = PxReconnectSupervisor::Create(async_runtime_, PxReconnectSupervisorOptions{
            .component = "obs_ipc",
            .connection_timeout = kIpcConnectionTimeout,
            .adapter_stop_timeout = std::chrono::seconds(3),
            .backoff = kIpcReconnectOptions,
        });
        if (!async_scope_ || !connection_supervisor_) {
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
            if (!self || self->exiting_.load(std::memory_order_acquire) || !self->ws_client_ || !self->connection_supervisor_) {
                return;
            }
            self->ws_client_->ws_stream().binary(true);
            self->ws_client_->set_no_delay(true);
        })
        .bind_connect([weak_self]() {
            if (asio2::get_last_error()) {
                LOGE("event=transport.connection_attempt component=obs_ipc code={} operation=connect outcome=failure "
                     "recoverable=true reason={}", asio2::last_error_val(), asio2::last_error_msg());
                if (const auto self = weak_self.lock(); self && self->connection_supervisor_) {
                    static_cast<void>(self->connection_supervisor_->FailActive(
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
                if (const auto self = weak_self.lock(); self && self->connection_supervisor_) {
                    static_cast<void>(self->connection_supervisor_->FailActive(
                        MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "ipc.upgrade", asio2::last_error_msg(), true)));
                }
            } else {
                if (const auto self = weak_self.lock(); self && self->connection_supervisor_) {
                    static_cast<void>(self->connection_supervisor_->MarkReady());
                }
            }
        })
        .bind_disconnect([weak_self]() {
            if (const auto self = weak_self.lock(); self && self->connection_supervisor_) {
                static_cast<void>(self->connection_supervisor_->MarkDisconnected(
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
        PxReconnectSupervisorHooks reconnect_hooks{
            .start_attempt = [client = ws_client_, port = port_, ipc_path = std::move(ipc_path)](std::uint64_t) {
                if (client->async_start("127.0.0.1", port, ipc_path)) {
                    return PxResult<void>::Success();
                }
                return PxResult<void>::Failure(MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected, "ipc.start", asio2::last_error_msg(), true));
            },
            .stop_attempt = [client = ws_client_](std::chrono::steady_clock::time_point deadline)
                -> PxAwaitable<PxResult<void>> {
                const auto requested = RequestAsioClientStop(client, "ipc.retry-reset");
                if (!requested) {
                    co_return requested;
                }
                co_return co_await WaitForAsioClientStopped(client, deadline, "ipc.retry-reset");
            },
        };
        if (!async_scope_->Spawn("ipc-connection-loop",
                                 [supervisor = connection_supervisor_, hooks = std::move(reconnect_hooks)]() mutable {
            return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
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
        const auto runtime = async_runtime_;
        const auto owner = weak_from_this().lock();
        if (runtime && runtime->IsRuntimeThread() && owner) {
            const auto weak_owner = std::weak_ptr<WsIpcClient>(owner);
            asio::co_spawn(
                runtime->Executor(PxAsyncLane::kControl),
                StopIpcFromRuntime(owner, deadline),
                [weak_owner](const std::exception_ptr& error) {
                    if (error) {
                        LOGE("event=async.scope_drain component=obs_ipc code=ASYNC_SCOPE_DRAIN_EXCEPTION "
                             "operation=stop_client outcome=failed recoverable=false");
                        return;
                    }
                    if (!weak_owner.lock()) {
                        LOGI("event=async.scope_drain component=obs_ipc operation=stop_client outcome=success owner=released");
                    }
                });
            LOGI("event=async.scope_drain component=obs_ipc operation=stop_client outcome=deferred "
                 "reason=shutdown_requested_from_runtime_thread");
            return;
        }

        const auto scope = BeginStop();
        const auto client = ws_client_;
        const auto remaining = std::max(
            std::chrono::milliseconds::zero(),
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
        const auto scope_drained = !scope || scope->WaitFor(remaining);
        static_cast<void>(RequestAsioClientStop(client, "ipc.adapter-stop-confirm"));
        const auto adapter_stopped = WaitForAsioClientStoppedBlocking(client, deadline);
        if (!scope_drained || !adapter_stopped) {
            LOGE("event=async.scope_drain component=obs_ipc code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                 "operation=stop_client outcome=timeout recoverable=false outstanding={}",
                 scope ? scope->GetStatistics().outstanding : 0);
            return;
        }
        if (runtime) {
            runtime->RequestDrain();
            runtime->Join();
        }
        FinishStop();
    }

    PxAwaitable<PxResult<void>> WsIpcClient::StopAsync(
        const std::shared_ptr<WsIpcClient>& owner,
        const std::chrono::steady_clock::time_point deadline) {
        if (!owner) {
            co_return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kInvalidArgument, "ipc.stop", "OBS IPC client owner is missing"));
        }
        const auto scope = owner->BeginStop();
        if (scope) {
            const auto drained = co_await WaitForAsyncScopeDrain(scope, deadline, "ipc.stop");
            if (!drained) {
                LOGE("event=async.scope_drain component=obs_ipc code={} operation=stop_client "
                     "outcome=failed recoverable={} outstanding={} reason={}",
                     drained.Error().StableCode(), drained.Error().retryable,
                     scope->GetStatistics().outstanding, drained.Error().message);
                co_return PxResult<void>::Failure(drained.Error());
            }
        }
        static_cast<void>(RequestAsioClientStop(owner->ws_client_, "ipc.adapter-stop-confirm"));
        const auto adapter_stopped = co_await WaitForAsioClientStopped(owner->ws_client_, deadline, "ipc.adapter-stop");
        if (!adapter_stopped) {
            co_return adapter_stopped;
        }
        const auto runtime = owner->async_runtime_;
        if (runtime) {
            runtime->RequestDrain();
            runtime->Join();
        }
        owner->FinishStop();
        co_return PxResult<void>::Success();
    }

    std::shared_ptr<PxAsyncScope> WsIpcClient::BeginStop() {
        exiting_.store(true, std::memory_order_release);
        if (incoming_messages_) {
            static_cast<void>(incoming_messages_->Close(
                MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "ipc.receive", "IPC websocket is stopping")));
        }
        if (connection_supervisor_) {
            connection_supervisor_->Stop();
        }
        const auto client = ws_client_;
        static_cast<void>(RequestAsioClientStop(client, "ipc.adapter-stop"));
        if (async_scope_) {
            async_scope_->BeginStop();
        }
        return async_scope_;
    }

    void WsIpcClient::FinishStop() {
        ws_client_.reset();
        incoming_messages_.reset();
        connection_supervisor_.reset();
        async_scope_.reset();
        async_runtime_.reset();
        started_.store(false, std::memory_order_release);
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
