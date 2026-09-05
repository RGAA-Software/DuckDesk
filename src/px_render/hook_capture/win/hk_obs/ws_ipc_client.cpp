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
#include "px_common/async_mailbox.h"
#include "px_common/asio_client_shutdown.h"
#include "px_common/async_scope_drain.h"
#include "px_common/reconnect_supervisor.h"
#include "px_common/websocket_reconnect_adapter.h"
#include "px_common/log.h"
#include "px_capture/capture_message.h"

namespace px {

namespace {

constexpr auto kIpcConnectionTimeout = std::chrono::seconds(10);
constexpr std::size_t kIpcMessageCapacity = 1024;
const PxReconnectBackoffOptions kIpcReconnectOptions{
    .initial_delay = std::chrono::milliseconds(100),
    .maximum_delay = std::chrono::seconds(5),
    .multiplier = 2.0,
    .jitter_ratio = 0.2,
};

template <typename T> T DecodeIpcValue(std::string_view bytes) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::array<char, sizeof(T)> storage{};
    std::copy_n(bytes.begin(), storage.size(), storage.begin());
    return std::bit_cast<T>(storage);
}

PxAwaitable<void> StopIpcFromRuntime(std::shared_ptr<WsIpcClient> owner, const std::chrono::steady_clock::time_point deadline) {
    const auto stopped = co_await WsIpcClient::StopAsync(owner, deadline);
    if (!stopped) {
        LOGE("event=async.scope_drain component=obs_ipc code={} operation=stop_client "
             "outcome=failed recoverable={} reason={}",
             stopped.Error().StableCode(), stopped.Error().retryable, stopped.Error().message);
        co_return;
    }
    LOGI("event=async.scope_drain component=obs_ipc operation=stop_client outcome=success");
}

} // namespace

std::shared_ptr<WsIpcClient> WsIpcClient::Make(int port) {
    return std::make_shared<WsIpcClient>(port);
}

WsIpcClient::WsIpcClient(int port) : port_(port), adapter_slot_(std::make_shared<PxReconnectAdapterSlot<asio2::ws_client>>()) {}

WsIpcClient::~WsIpcClient() {
    Exit();
}

bool WsIpcClient::IsStarted() const {
    return started_.load(std::memory_order_acquire) && !exiting_.load(std::memory_order_acquire);
}

bool WsIpcClient::IsConnected() const {
    const auto state = SnapshotAsyncState();
    const auto client = adapter_slot_->Snapshot();
    return IsStarted() && client && client->is_started() && state.supervisor && state.supervisor->IsReady();
}

std::uint64_t WsIpcClient::ConnectionGeneration() const {
    const auto state = SnapshotAsyncState();
    return state.supervisor ? state.supervisor->Generation() : 0;
}

void WsIpcClient::Start() {
    std::unique_lock operation_lock(operation_mutex_);
    if (started_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    exiting_.store(false, std::memory_order_release);
    const auto async_runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    if (!async_runtime || !async_runtime->Start()) {
        LOGE("ws ipc client failed to start async runtime");
        exiting_.store(true, std::memory_order_release);
        started_.store(false, std::memory_order_release);
        return;
    }
    const auto async_scope = PxAsyncScope::Create(async_runtime, PxAsyncLane::kState);
    const auto connection_supervisor = PxReconnectSupervisor::Create(async_runtime, PxReconnectSupervisorOptions{
                                                                                        .component = "obs_ipc",
                                                                                        .connection_timeout = kIpcConnectionTimeout,
                                                                                        .adapter_stop_timeout = std::chrono::seconds(3),
                                                                                        .backoff = kIpcReconnectOptions,
                                                                                    });
    const auto incoming_messages = async_scope ? PxAsyncMailbox<std::string>::Create(async_scope->Executor(), kIpcMessageCapacity)
                                               : std::shared_ptr<PxAsyncMailbox<std::string>>{};
    if (!async_scope || !connection_supervisor || !incoming_messages) {
        LOGE("ws ipc client failed to create async workflow");
        async_runtime->RequestDrain();
        async_runtime->Join();
        exiting_.store(true, std::memory_order_release);
        started_.store(false, std::memory_order_release);
        return;
    }
    {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        async_runtime_ = async_runtime;
        async_scope_ = async_scope;
        connection_supervisor_ = connection_supervisor;
        incoming_messages_ = incoming_messages;
    }

    const auto weak_self = weak_from_this();
    if (!async_scope->Spawn("ipc-receive-loop", [weak_self, mailbox = incoming_messages]() { return RunIncomingMessageLoop(weak_self, mailbox); })) {
        LOGE("ws ipc client failed to start receive coroutine");
        operation_lock.unlock();
        Exit();
        return;
    }

    // Identify ourselves by pid: the render only accepts /ipc connections from
    // pids it registered via RegisterIpcPid (wrote hook boot config for them).
    std::string ipc_path = std::format("/ipc?pid={}", GetCurrentProcessId());
    LOGI("ws ipc client starting: 127.0.0.1:{}{}", port_, ipc_path);
    const auto adapter_slot = adapter_slot_;
    const auto supervisor = connection_supervisor;
    const auto mailbox = incoming_messages;
    PxReconnectSupervisorHooks reconnect_hooks{
        .start_attempt =
            [weak_self, adapter_slot, supervisor, mailbox, port = port_, ipc_path = std::move(ipc_path)](const std::uint64_t generation) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_.load(std::memory_order_acquire)) {
                    return PxResult<void>::Failure(
                        MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "ipc.start", "IPC websocket owner is stopping"));
                }
                const auto client = std::make_shared<asio2::ws_client>();
                const auto weak_client = std::weak_ptr<asio2::ws_client>(client);
                client->set_auto_reconnect(false);
                client->set_timeout(std::chrono::seconds(2));
                client
                    ->bind_init([weak_self, weak_client]() {
                        const auto owner = weak_self.lock();
                        const auto current = weak_client.lock();
                        if (!owner || !current || owner->exiting_.load(std::memory_order_acquire)) {
                            return;
                        }
                        current->ws_stream().binary(true);
                        current->set_no_delay(true);
                    })
                    .bind_connect([weak_self, supervisor, generation, port]() {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire)) {
                            if (asio2::get_last_error()) {
                                static_cast<void>(supervisor->FailActive(generation, MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected,
                                                                                                      "ipc.connect", asio2::last_error_msg(), true)));
                                return;
                            }
                            LOGI("ws ipc client connected to 127.0.0.1:{}/ipc", port);
                        }
                    })
                    .bind_upgrade([weak_self, supervisor, generation]() {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire)) {
                            if (asio2::get_last_error()) {
                                static_cast<void>(supervisor->FailActive(
                                    generation, MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "ipc.upgrade", asio2::last_error_msg(), true)));
                                return;
                            }
                            static_cast<void>(supervisor->MarkReady(generation));
                        }
                    })
                    .bind_disconnect([weak_self, supervisor, generation]() {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire)) {
                            static_cast<void>(
                                supervisor->MarkDisconnected(generation, MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "ipc.disconnect",
                                                                                          "IPC websocket disconnected", true)));
                        }
                    })
                    .bind_recv([weak_self, mailbox](std::string_view data) {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire)) {
                            static_cast<void>(mailbox->TryPush(std::string(data)));
                        }
                    });
                adapter_slot->Replace(client);
                return StartWebSocketAdapter(client, "127.0.0.1", port, ipc_path, "ipc.start");
            },
        .stop_attempt =
            [adapter_slot](const std::chrono::steady_clock::time_point deadline) {
                return StopWebSocketAdapter(adapter_slot->Snapshot(), deadline, "ipc.retry-reset");
            },
    };
    if (!async_scope->Spawn("ipc-connection-loop", [supervisor, hooks = std::move(reconnect_hooks)]() mutable {
            return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
        })) {
        LOGE("ws ipc client failed to start connection coroutine");
        operation_lock.unlock();
        Exit();
    }
}

void WsIpcClient::Exit() {
    std::unique_lock operation_lock(operation_mutex_);
    if (exiting_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const auto state = SnapshotAsyncState();
    const auto runtime = state.runtime;
    const auto owner = weak_from_this().lock();
    if (runtime && runtime->IsRuntimeThread() && owner) {
        const auto weak_owner = std::weak_ptr<WsIpcClient>(owner);
        asio::co_spawn(runtime->Executor(PxAsyncLane::kControl), StopIpcFromRuntime(owner, deadline), [weak_owner](const std::exception_ptr& error) {
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
    const auto client = adapter_slot_->Snapshot();
    const auto remaining = std::max(std::chrono::milliseconds::zero(),
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

PxAwaitable<PxResult<void>> WsIpcClient::StopAsync(std::shared_ptr<WsIpcClient> owner, const std::chrono::steady_clock::time_point deadline) {
    if (!owner) {
        co_return PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "ipc.stop", "OBS IPC client owner is missing"));
    }
    std::shared_ptr<PxAsyncScope> scope;
    AsyncStateSnapshot state;
    {
        std::unique_lock operation_lock(owner->operation_mutex_);
        state = owner->SnapshotAsyncState();
        scope = owner->BeginStop();
    }
    if (scope) {
        const auto drained = co_await WaitForAsyncScopeDrain(scope, deadline, "ipc.stop");
        if (!drained) {
            LOGE("event=async.scope_drain component=obs_ipc code={} operation=stop_client "
                 "outcome=failed recoverable={} outstanding={} reason={}",
                 drained.Error().StableCode(), drained.Error().retryable, scope->GetStatistics().outstanding, drained.Error().message);
            co_return PxResult<void>::Failure(drained.Error());
        }
    }
    const auto client = owner->adapter_slot_->Snapshot();
    static_cast<void>(RequestAsioClientStop(client, "ipc.adapter-stop-confirm"));
    const auto adapter_stopped = co_await WaitForAsioClientStopped(client, deadline, "ipc.adapter-stop");
    if (!adapter_stopped) {
        co_return adapter_stopped;
    }
    if (state.runtime) {
        state.runtime->RequestDrain();
        state.runtime->Join();
    }
    {
        std::unique_lock operation_lock(owner->operation_mutex_);
        owner->FinishStop();
    }
    co_return PxResult<void>::Success();
}

std::shared_ptr<PxAsyncScope> WsIpcClient::BeginStop() {
    const auto state = SnapshotAsyncState();
    exiting_.store(true, std::memory_order_release);
    if (state.mailbox) {
        static_cast<void>(state.mailbox->Close(MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "ipc.receive", "IPC websocket is stopping")));
    }
    if (state.supervisor) {
        state.supervisor->Stop();
    }
    const auto client = adapter_slot_->Snapshot();
    static_cast<void>(RequestAsioClientStop(client, "ipc.adapter-stop"));
    if (state.scope) {
        state.scope->BeginStop();
    }
    return state.scope;
}

void WsIpcClient::FinishStop() {
    adapter_slot_->Clear();
    {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        incoming_messages_.reset();
        connection_supervisor_.reset();
        async_scope_.reset();
        async_runtime_.reset();
    }
    started_.store(false, std::memory_order_release);
}

WsIpcClient::AsyncStateSnapshot WsIpcClient::SnapshotAsyncState() const {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    return AsyncStateSnapshot{
        .runtime = async_runtime_,
        .scope = async_scope_,
        .supervisor = connection_supervisor_,
        .mailbox = incoming_messages_,
    };
}

void WsIpcClient::RegisterIpcMessageCallback(WsIpcMessageCallback&& callback) {
    std::lock_guard lock(callback_mutex_);
    ipc_cbk_ = std::move(callback);
}

void WsIpcClient::PostIpcMessage(const std::string& msg) {
    const auto client = adapter_slot_->Snapshot();
    if (!client) {
        const auto count = null_drop_count_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (count == 1 || (count % 200) == 0) {
            LOGE("ws ipc PostIpcMessage: client is null, drop {} bytes n={}", msg.size(), count);
        }
        return;
    }
    if (!client->is_started()) {
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
    client->async_send(msg);
}

PxAwaitable<void> WsIpcClient::RunIncomingMessageLoop(std::weak_ptr<WsIpcClient> weak_client, std::shared_ptr<PxAsyncMailbox<std::string>> mailbox) {
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
    } else if (base_message.type_ == kKeyboardEventMessage) {
        if (msg.size() != sizeof(KeyboardEventMessage)) {
            LOGE("msg size != sizeof(KeyboardEventMessage), msg size: {}, event size: {}", msg.size(), sizeof(KeyboardEventMessage));
            return;
        }
        callback(std::make_shared<KeyboardEventMessage>(DecodeIpcValue<KeyboardEventMessage>(msg)));
    } else if (base_message.type_ == kCaptureResetInputMessage) {
        if (msg.size() != sizeof(CaptureResetInputMessage)) {
            LOGE("msg size != sizeof(CaptureResetInputMessage), msg size: {}", msg.size());
            return;
        }
        callback(std::make_shared<CaptureResetInputMessage>(DecodeIpcValue<CaptureResetInputMessage>(msg)));
    }
}

} // namespace px
