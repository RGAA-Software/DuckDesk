//
// Created by RGAA on 28/02/2025.
//

#include "relay_ws_client.h"
#include "px_common/data.h"
#include "px_common/log.h"
#include "px_common/thread_util.h"
#include "px_common/string_util.h"
#include "px_common/asio_client_shutdown.h"
#include "px_common/reconnect_supervisor.h"
#include "px_common/websocket_reconnect_adapter.h"
#include <asio2/websocket/ws_client.hpp>
#include <asio2/asio2.hpp>
#include "relay_message.pb.h"

using namespace px_relay;

namespace px
{
    namespace {

        std::shared_ptr<PxAsyncRuntime> SharedRelayReconnectRuntime() {
            static const auto runtime = [] {
                const auto value = PxAsyncRuntime::Create({.worker_threads = 1});
                if (!value || !value->Start()) {
                    return std::shared_ptr<PxAsyncRuntime>{};
                }
                return value;
            }();
            return runtime;
        }

    } // namespace

    RelayWsClient::RelayWsClient(const std::string& host, int port, const std::string& device_id,
                                 const std::string& device_name, const std::string& stream_id,
                                 const std::string& appkey, bool force_gdi, const std::string& remote_device_id,
                                 const std::string& connection_ticket, const std::string& connection_nonce,
                                 std::shared_ptr<PxAsyncRuntime> runtime)
                                 : RelayNetClient(), adapter_slot_(std::make_shared<PxReconnectAdapterSlot<asio2::ws_client>>()),
                                   async_runtime_(std::move(runtime)) {
        this->host_ = host;
        this->port_ = port;
        this->device_id_ = device_id;
        this->device_name_ = device_name;
        StringUtil::Replace(this->device_name_, " ", "");
        this->stream_id_ = stream_id;
        this->appkey_ = appkey;
        this->remote_device_id_ = remote_device_id;
        this->force_gdi_ = force_gdi;
        this->connection_ticket_ = connection_ticket;
        this->connection_nonce_ = connection_nonce;
    }

    RelayWsClient::~RelayWsClient() {
        Stop();
    }

    void RelayWsClient::Start() {
        std::unique_lock operation_lock(operation_mutex_);
        if (started_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        exiting_.store(false, std::memory_order_release);
        if (!async_runtime_) {
            async_runtime_ = SharedRelayReconnectRuntime();
        }
        {
            std::lock_guard lock(stop_mutex_);
            connection_scope_ = PxAsyncScope::Create(async_runtime_, PxAsyncLane::kState);
            reconnect_supervisor_ = PxReconnectSupervisor::Create(async_runtime_, MakeWebSocketReconnectOptions("relay_ws"));
        }
        if (!adapter_slot_ || !connection_scope_ || !reconnect_supervisor_) {
            LOGE("event=module.start component=relay_ws code=ASYNC_WORKFLOW_CREATE_FAILED "
                 "operation=start_client outcome=failed recoverable=false");
            operation_lock.unlock();
            Stop();
            return;
        }
        const auto weak_self = weak_from_this();
        // the /ws is the websocket upgraged target
        auto ws_path = std::format("/relay?device_id={}&remote_device_id={}&device_name={}&stream_id={}&appkey={}",
                                   device_id_, remote_device_id_, device_name_, stream_id_, appkey_);
        if (!connection_ticket_.empty()) {
            ws_path += std::format("&file_only=1&ticket={}&client_nonce={}", connection_ticket_, connection_nonce_);
        }
        LOGI("Will connect relay websocket: {}:{}, device: {}, remote: {}, authenticated file-only: {}",
             host_, port_, device_id_, remote_device_id_, !connection_ticket_.empty());
        const auto adapter_slot = adapter_slot_;
        const auto supervisor = reconnect_supervisor_;
        PxReconnectSupervisorHooks hooks{
            .start_attempt = [weak_self, adapter_slot, supervisor, host = host_, port = port_, path = std::move(ws_path)](
                                 const std::uint64_t generation) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return PxResult<void>::Failure(MakePxAsyncError(
                        PxAsyncErrorCode::kServiceStopped, "relay-ws.start", "Relay websocket owner is stopping"));
                }
                const auto client = std::make_shared<asio2::ws_client>();
                const auto weak_client = std::weak_ptr<asio2::ws_client>(client);
                client->set_auto_reconnect(false);
                client->set_timeout(std::chrono::milliseconds(3000));
                client->bind_init([weak_self, weak_client]() {
                    const auto owner = weak_self.lock();
                    const auto current = weak_client.lock();
                    if (!owner || !current || owner->exiting_.load(std::memory_order_acquire)) {
                        return;
                    }
                    current->set_no_delay(true);
                    current->ws_stream().set_option(websocket::stream_base::decorator([](websocket::request_type& request) {
                        request.set(http::field::authorization, "websocket-client-authorization");
                    }));
                }).bind_connect([weak_self, weak_client, supervisor, generation]() {
                    const auto owner = weak_self.lock();
                    const auto current = weak_client.lock();
                    if (!owner || !current || owner->exiting_.load(std::memory_order_acquire)) {
                        return;
                    }
                    if (asio2::get_last_error()) {
                        static_cast<void>(supervisor->FailActive(generation, MakePxAsyncError(
                            PxAsyncErrorCode::kServiceNotConnected, "relay-ws.connect", asio2::last_error_msg(), true)));
                        return;
                    }
                    LOGI("connect success : {} {} ", current->local_address().c_str(), current->local_port());
                }).bind_disconnect([weak_self, supervisor, generation]() {
                    if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire)) {
                        static_cast<void>(supervisor->MarkDisconnected(generation, MakePxAsyncError(
                            PxAsyncErrorCode::kServiceNotConnected, "relay-ws.disconnect", "Relay websocket disconnected", true)));
                    }
                }).bind_upgrade([weak_self, supervisor, generation]() {
                    if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire)) {
                        if (asio2::get_last_error()) {
                            static_cast<void>(supervisor->FailActive(generation, MakePxAsyncError(
                                PxAsyncErrorCode::kProtocolError, "relay-ws.upgrade", asio2::last_error_msg(), true)));
                            return;
                        }
                        static_cast<void>(supervisor->MarkReady(generation));
                    }
                }).bind_recv([weak_self](std::string_view data) {
                    if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire) && owner->msg_cbk_) {
                        owner->msg_cbk_(Data::From(std::string(data)));
                    }
                });
                adapter_slot->Replace(client);
                return StartWebSocketAdapter(client, host, port, path, "relay-ws.start");
            },
            .stop_attempt = [adapter_slot](const std::chrono::steady_clock::time_point deadline) {
                return StopWebSocketAdapter(adapter_slot->Snapshot(), deadline, "relay-ws.retry-reset");
            },
            .on_ready = [weak_self](std::uint64_t) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                self->NotifyFileTransferWritable();
                if (self->srv_conn_cbk_) {
                    self->srv_conn_cbk_();
                }
                self->SendHello();
                const auto client = self->adapter_slot_->Snapshot();
                if (client) {
                    client->start_timer("ws-heartbeat", std::chrono::seconds(1), [weak_self]() {
                    if (const auto current = weak_self.lock(); current && !current->exiting_) {
                        current->HeartBeat();
                    }
                    });
                }
            },
            .on_lost = [weak_self](std::uint64_t, const PxAsyncError&, const bool was_ready) {
                if (const auto self = weak_self.lock(); self && was_ready && !self->exiting_) {
                    self->NotifyFileTransferClosed();
                    if (self->srv_dis_conn_cbk_) {
                        self->srv_dis_conn_cbk_();
                    }
                }
            },
        };
        if (!connection_scope_->Spawn("relay-ws-reconnect", [supervisor = reconnect_supervisor_, hooks = std::move(hooks)]() mutable {
                return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
            })) {
            LOGE("event=module.start component=relay_ws code=ASYNC_SCOPE_SPAWN_FAILED "
                 "operation=start_reconnect outcome=failed recoverable=false");
            operation_lock.unlock();
            Stop();
        }
    }

    void RelayWsClient::Stop() {
        std::unique_lock operation_lock(operation_mutex_);
        NotifyFileTransferClosed();
        exiting_.store(true, std::memory_order_release);
        std::shared_ptr<asio2::ws_client> client;
        std::shared_ptr<PxAsyncScope> scope;
        std::shared_ptr<PxReconnectSupervisor> supervisor;
        {
            std::lock_guard lock(stop_mutex_);
            client = adapter_slot_->Snapshot();
            scope = connection_scope_;
            supervisor = reconnect_supervisor_;
        }
        if (supervisor) {
            supervisor->Stop();
        }
        static_cast<void>(RequestAsioClientStop(client, "relay-ws.stop"));
        if (scope) {
            scope->BeginStop();
            if (scope->IsScopeThread()) {
                ScheduleDeferredStop();
                return;
            }
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        const auto scope_drained = !scope || scope->WaitFor(std::chrono::seconds(5));
        static_cast<void>(RequestAsioClientStop(client, "relay-ws.stop-confirm"));
        const auto adapter_stopped = WaitForAsioClientStoppedBlocking(client, deadline);
        if (!scope_drained || !adapter_stopped) {
            LOGE("event=async.scope_drain component=relay_ws code=ASYNC_SCOPE_DRAIN_TIMEOUT operation=stop_client "
                 "outcome=timeout recoverable=false scope_drained={} adapter_stopped={} outstanding={}",
                 scope_drained, adapter_stopped, scope ? scope->GetStatistics().outstanding : 0);
            return;
        }
        FinishStop();
    }

    void RelayWsClient::FinishStop() {
        std::lock_guard lock(stop_mutex_);
        if (connection_scope_ && connection_scope_->GetStatistics().outstanding != 0) {
            return;
        }
        adapter_slot_->Clear();
        reconnect_supervisor_.reset();
        connection_scope_.reset();
        started_.store(false, std::memory_order_release);
        deferred_stop_scheduled_.store(false, std::memory_order_release);
    }

    void RelayWsClient::ScheduleDeferredStop() {
        if (deferred_stop_scheduled_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        const auto weak_self = weak_from_this();
        const auto runtime = async_runtime_;
        if (!runtime || !runtime->DeferBlocking([weak_self] {
            if (const auto self = weak_self.lock()) {
                self->Stop();
            }
        })) {
            deferred_stop_scheduled_.store(false, std::memory_order_release);
            LOGE("event=async.scope_drain component=relay_ws code=ASYNC_DEFER_FAILED operation=stop_client "
                 "outcome=failed recoverable=false");
        }
    }

    void RelayWsClient::PostBinaryMessage(const std::string &msg) {
        std::lock_guard<std::mutex> guard(send_mtx_);
        std::shared_ptr<asio2::ws_client> client;
        std::shared_ptr<PxReconnectSupervisor> supervisor;
        {
            std::lock_guard lock(stop_mutex_);
            client = adapter_slot_->Snapshot();
            supervisor = reconnect_supervisor_;
        }
        if (exiting_ || !client || !client->is_started() || !supervisor || !supervisor->IsReady()) {
            return;
        }
        client->ws_stream().binary(true);
        queuing_msg_count_++;
        const auto weak_self = weak_from_this();
        client->async_send(msg, [weak_self]() {
            if (const auto self = weak_self.lock()) {
                --self->queuing_msg_count_;
                if (self->GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) {
                    self->NotifyFileTransferWritable();
                }
            }
        });
    }

    bool RelayWsClient::IsAlive() {
        std::lock_guard lock(stop_mutex_);
        const auto client = adapter_slot_->Snapshot();
        return !exiting_ && client && client->is_started() && reconnect_supervisor_ && reconnect_supervisor_->IsReady();
    }

    std::uint64_t RelayWsClient::ConnectionGeneration() const {
        std::lock_guard lock(stop_mutex_);
        return reconnect_supervisor_ ? reconnect_supervisor_->Generation() : 0;
    }

    void RelayWsClient::SyncDeviceId(const std::string& device_id) {
        this->device_id_ = device_id;
    }

    void RelayWsClient::SendHello() {
        if (!IsAlive()) {
            return;
        }
        RelayMessage rl_msg;
        rl_msg.set_type(RelayMessageType::kRelayHello);
        rl_msg.set_from_device_id(this->device_id_);
        auto& sub = *rl_msg.mutable_hello();
        for (const auto& info : net_info_) {
            auto& net_info = *sub.mutable_net_info()->Add();
            net_info.set_ip(info.ip_);
            net_info.set_mac(info.mac_);
        }
        auto msg = rl_msg.SerializeAsString();
        PostBinaryMessage(msg);
    }

    void RelayWsClient::HeartBeat() {
        if (!IsAlive()) {
            return;
        }
        RelayMessage rl_msg;
        rl_msg.set_type(RelayMessageType::kRelayHeartBeat);
        rl_msg.set_from_device_id(this->device_id_);
        auto& sub = *rl_msg.mutable_heartbeat();
        sub.set_index(heartbeat_index_.fetch_add(1, std::memory_order_acq_rel));
        for (const auto& info : net_info_) {
            auto& net_info = *sub.mutable_net_info()->Add();
            net_info.set_ip(info.ip_);
            net_info.set_mac(info.mac_);
        }
        auto msg = rl_msg.SerializeAsString();
        PostBinaryMessage(msg);
    }

    void RelayWsClient::SetDeviceNetInfo(const std::vector<px::RelayDeviceNetInfo>& info) {
        net_info_ = info;
    }

    int64_t RelayWsClient::GetQueuingMsgCount() {
        std::shared_ptr<asio2::ws_client> client;
        std::shared_ptr<PxReconnectSupervisor> supervisor;
        {
            std::lock_guard lock(stop_mutex_);
            client = adapter_slot_->Snapshot();
            supervisor = reconnect_supervisor_;
        }
        if (exiting_ || !client || !client->is_started() || !supervisor || !supervisor->IsReady()) {
            return 0;
        }
        return std::max(static_cast<int64_t>(queuing_msg_count_), static_cast<int64_t>(client->get_pending_event_count()));
    }

    void RelayWsClient::PostNetTask(std::function<void ()> &&task) {
        std::shared_ptr<asio2::ws_client> client;
        std::shared_ptr<PxReconnectSupervisor> supervisor;
        {
            std::lock_guard lock(stop_mutex_);
            client = adapter_slot_->Snapshot();
            supervisor = reconnect_supervisor_;
        }
        if (!exiting_ && client && client->is_started() && supervisor && supervisor->IsReady()) {
            client->post_queued_event([t = std::move(task)]() {
                t();
            });
        }
    }

    std::shared_ptr<FileTransferWritableSignal>
    RelayWsClient::AcquireFileTransferWritableSignal() {
        std::shared_ptr<FileTransferWritableSignal> signal;
        {
            std::lock_guard lock(writable_signal_mutex_);
            if (!writable_signal_ ||
                writable_signal_->outcome() != FileTransferWritableOutcome::kPending) {
                writable_signal_ = FileTransferWritableSignal::Create();
            }
            signal = writable_signal_;
        }
        if (GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) {
            signal->NotifyWritable();
        }
        return signal;
    }

    void RelayWsClient::NotifyFileTransferWritable() {
        std::shared_ptr<FileTransferWritableSignal> signal;
        {
            std::lock_guard lock(writable_signal_mutex_);
            signal = std::move(writable_signal_);
        }
        if (signal) {
            signal->NotifyWritable();
        }
    }

    void RelayWsClient::NotifyFileTransferClosed() {
        std::shared_ptr<FileTransferWritableSignal> signal;
        {
            std::lock_guard lock(writable_signal_mutex_);
            signal = std::move(writable_signal_);
        }
        if (signal) {
            signal->Close();
        }
    }

}
