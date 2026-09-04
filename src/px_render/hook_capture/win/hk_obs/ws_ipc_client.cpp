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
#include "px_common_new/connection_attempt_workflow.h"
#include "px_common_new/log.h"
#include "px_capture_new/capture_message.h"

namespace px
{

    namespace {

        constexpr auto kIpcConnectionTimeout = std::chrono::seconds(10);
        constexpr std::size_t kIpcMessageCapacity = 1024;

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
        if (!async_scope_ || !connection_workflow_) {
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
        ws_client_->set_auto_reconnect(true);
        ws_client_->set_timeout(std::chrono::seconds(2));
        ws_client_->bind_init([weak_self]() {
            const auto self = weak_self.lock();
            if (!self || self->exiting_.load(std::memory_order_acquire) || !self->ws_client_ || !self->connection_workflow_) {
                return;
            }
            self->ws_client_->ws_stream().binary(true);
            self->ws_client_->set_no_delay(true);
            auto attempt = self->connection_workflow_->StartAttempt();
            if (!attempt) {
                return;
            }
            const auto ticket = attempt.Value();
            self->connection_generation_.store(ticket.generation, std::memory_order_release);
            if (!self->async_scope_->Spawn("ipc-connection-attempt", [weak_self, workflow = self->connection_workflow_, ticket]() {
                    return WaitForConnectionReady(weak_self, workflow, ticket, std::chrono::steady_clock::now() + kIpcConnectionTimeout);
                })) {
                static_cast<void>(self->connection_workflow_->FailActive(
                    ticket.generation, MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "ipc.connect", "connection scope rejected attempt")));
            }
        })
        .bind_connect([weak_self]() {
            if (asio2::get_last_error()) {
                LOGE("ws ipc client connect failed: {} {}",
                     asio2::last_error_val(), asio2::last_error_msg());
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
                LOGE("ws ipc upgrade failed: {}", asio2::last_error_msg());
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
                static_cast<void>(self->connection_workflow_->FailActive(
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
        if (!ws_client_->async_start("127.0.0.1", port_, ipc_path)) {
            LOGE("ws ipc async_start failure: {} {}",
                 asio2::last_error_val(), asio2::last_error_msg());
        }
    }

    void WsIpcClient::Exit() {
        if (exiting_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        if (incoming_messages_) {
            static_cast<void>(incoming_messages_->Close(
                MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "ipc.receive", "IPC websocket is stopping")));
        }
        if (async_scope_) {
            async_scope_->BeginStop();
            if (!async_scope_->IsScopeThread()) {
                static_cast<void>(async_scope_->WaitFor(std::chrono::seconds(2)));
            }
        }
        if (connection_workflow_) {
            connection_workflow_->Stop();
        }
        const auto client = ws_client_;
        if (client) {
            client->post([client]() {
                client->set_auto_reconnect(false);
                client->stop_all_timers();
                client->stop();
            });
        }
        if (async_runtime_) {
            async_runtime_->RequestStop();
            async_runtime_->Join();
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

    PxAwaitable<void> WsIpcClient::WaitForConnectionReady(
        std::weak_ptr<WsIpcClient> weak_client,
        std::shared_ptr<PxConnectionAttemptWorkflow> workflow,
        PxConnectionAttemptTicket ticket,
        std::chrono::steady_clock::time_point deadline) {
        auto result = co_await PxConnectionAttemptWorkflow::WaitUntilReady(std::move(workflow), ticket, deadline);
        const auto self = weak_client.lock();
        if (!self || self->exiting_.load(std::memory_order_acquire)) {
            co_return;
        }
        if (result) {
            LOGI("ws ipc upgrade success, generation={}", result.Value().generation);
        } else {
            LOGW("ws ipc connection attempt ended: generation={}, stage={}, code={}",
                 ticket.generation, result.Error().stage, result.Error().StableCode());
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
