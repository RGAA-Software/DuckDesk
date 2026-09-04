//
// Created by RGAA on 28/02/2025.
//

#include "relay_ws_client.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/thread_util.h"
#include "px_common_new/string_util.h"
#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/reconnect_supervisor.h"
#include "px_common_new/websocket_reconnect_adapter.h"
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
                                 : RelayNetClient(), async_runtime_(std::move(runtime)) {
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
        if (started_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        exiting_.store(false, std::memory_order_release);
        if (!async_runtime_) {
            async_runtime_ = SharedRelayReconnectRuntime();
        }
        client_ = std::make_shared<asio2::ws_client>();
        connection_scope_ = PxAsyncScope::Create(async_runtime_, PxAsyncLane::kState);
        reconnect_supervisor_ = PxReconnectSupervisor::Create(
            async_runtime_, MakeWebSocketReconnectOptions("relay_ws"));
        if (!client_ || !connection_scope_ || !reconnect_supervisor_) {
            LOGE("event=module.start component=relay_ws code=ASYNC_WORKFLOW_CREATE_FAILED "
                 "operation=start_client outcome=failed recoverable=false");
            Stop();
            return;
        }
        const auto weak_self = weak_from_this();
        client_->set_auto_reconnect(false);
        client_->set_timeout(std::chrono::milliseconds(3000));

        client_->bind_init([weak_self]() {
            if (const auto self = weak_self.lock(); self && self->client_) {
                self->client_->set_no_delay(true);
                self->client_->ws_stream().set_option(
                    websocket::stream_base::decorator([](websocket::request_type &req) {
                        req.set(http::field::authorization, "websocket-client-authorization");
                    })
                );
            }
        })
        .bind_connect([weak_self]() {
            if (const auto self = weak_self.lock(); self && self->client_) {
                if (asio2::get_last_error()) {
                    LOGW("event=transport.connection_attempt component=relay_ws code={} operation=connect "
                         "outcome=failed recoverable=true reason={}",
                         asio2::last_error_val(), asio2::last_error_msg());
                    static_cast<void>(self->reconnect_supervisor_->FailActive(MakePxAsyncError(
                        PxAsyncErrorCode::kServiceNotConnected, "relay-ws.connect", asio2::last_error_msg(), true)));
                } else {
                    LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
                }
            }
        })
        .bind_disconnect([weak_self]() {
            if (const auto self = weak_self.lock(); self && !self->exiting_ && self->reconnect_supervisor_) {
                static_cast<void>(self->reconnect_supervisor_->MarkDisconnected(MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected, "relay-ws.disconnect", "Relay websocket disconnected", true)));
            }
        })
        .bind_upgrade([weak_self]() {
            if (asio2::get_last_error()) {
                if (const auto self = weak_self.lock(); self && !self->exiting_ && self->reconnect_supervisor_) {
                    static_cast<void>(self->reconnect_supervisor_->FailActive(MakePxAsyncError(
                        PxAsyncErrorCode::kProtocolError, "relay-ws.upgrade", asio2::last_error_msg(), true)));
                }
                return;
            }
            if (const auto self = weak_self.lock(); self && !self->exiting_ && self->reconnect_supervisor_) {
                static_cast<void>(self->reconnect_supervisor_->MarkReady());
            }
        })
        .bind_recv([weak_self](std::string_view data) {
            if (const auto self = weak_self.lock(); self && self->client_ && self->msg_cbk_) {
                self->msg_cbk_(Data::From(std::string(data)));
            }
        });

        // the /ws is the websocket upgraged target
        auto ws_path = std::format("/relay?device_id={}&remote_device_id={}&device_name={}&stream_id={}&appkey={}",
                                   device_id_, remote_device_id_, device_name_, stream_id_, appkey_);
        if (!connection_ticket_.empty()) {
            ws_path += std::format("&file_only=1&ticket={}&client_nonce={}", connection_ticket_, connection_nonce_);
        }
        LOGI("Will connect relay websocket: {}:{}, device: {}, remote: {}, authenticated file-only: {}",
             host_, port_, device_id_, remote_device_id_, !connection_ticket_.empty());
        PxReconnectSupervisorHooks hooks{
            .start_attempt = [client = client_, host = host_, port = port_, path = std::move(ws_path)](std::uint64_t) {
                return StartWebSocketAdapter(client, host, port, path, "relay-ws.start");
            },
            .stop_attempt = [client = client_](const std::chrono::steady_clock::time_point deadline) {
                return StopWebSocketAdapter(client, deadline, "relay-ws.retry-reset");
            },
            .on_ready = [weak_self](std::uint64_t) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_ || !self->client_) {
                    return;
                }
                self->NotifyFileTransferWritable();
                if (self->srv_conn_cbk_) {
                    self->srv_conn_cbk_();
                }
                self->SendHello();
                self->client_->start_timer("ws-heartbeat", std::chrono::seconds(1), [weak_self]() {
                    if (const auto current = weak_self.lock(); current && !current->exiting_) {
                        current->HeartBeat();
                    }
                });
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
            Stop();
        }
    }

    void RelayWsClient::Stop() {
        NotifyFileTransferClosed();
        exiting_.store(true, std::memory_order_release);
        if (reconnect_supervisor_) {
            reconnect_supervisor_->Stop();
        }
        static_cast<void>(RequestAsioClientStop(client_, "relay-ws.stop"));
        if (connection_scope_) {
            connection_scope_->BeginStop();
            if (connection_scope_->IsScopeThread()) {
                ScheduleDeferredStop();
                return;
            }
            static_cast<void>(connection_scope_->WaitFor(std::chrono::seconds(5)));
        }
        static_cast<void>(RequestAsioClientStop(client_, "relay-ws.stop-confirm"));
        static_cast<void>(WaitForAsioClientStoppedBlocking(
            client_, std::chrono::steady_clock::now() + std::chrono::seconds(3)));
        FinishStop();
    }

    void RelayWsClient::FinishStop() {
        std::lock_guard lock(stop_mutex_);
        if (connection_scope_ && connection_scope_->GetStatistics().outstanding != 0) {
            return;
        }
        client_.reset();
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
        asio::post(asio::system_executor{}, [weak_self] {
            if (const auto self = weak_self.lock()) {
                self->Stop();
            }
        });
    }

    void RelayWsClient::PostBinaryMessage(const std::string &msg) {
        std::lock_guard<std::mutex> guard(send_mtx_);
        if (!IsAlive()) {
            return;
        }
        client_->ws_stream().binary(true);
        queuing_msg_count_++;
        const auto weak_self = weak_from_this();
        client_->async_send(msg, [weak_self]() {
            if (const auto self = weak_self.lock()) {
                --self->queuing_msg_count_;
                if (self->GetQueuingMsgCount() <= kFileTransferQueueLowWatermark) {
                    self->NotifyFileTransferWritable();
                }
            }
        });
    }

    bool RelayWsClient::IsAlive() {
        return !exiting_ && client_ && client_->is_started()
            && reconnect_supervisor_ && reconnect_supervisor_->IsReady();
    }

    std::uint64_t RelayWsClient::ConnectionGeneration() const {
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
        auto sub = rl_msg.mutable_hello();
        for (const auto& info : net_info_) {
            auto ni = sub->mutable_net_info()->Add();
            ni->set_ip(info.ip_);
            ni->set_mac(info.mac_);
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
        auto sub = rl_msg.mutable_heartbeat();
        static int64_t hb_ibx = 0;
        sub->set_index(hb_ibx++);
        for (const auto& info : net_info_) {
            auto ni = sub->mutable_net_info()->Add();
            ni->set_ip(info.ip_);
            ni->set_mac(info.mac_);
        }
        auto msg = rl_msg.SerializeAsString();
        PostBinaryMessage(msg);
    }

    void RelayWsClient::SetDeviceNetInfo(const std::vector<px::RelayDeviceNetInfo>& info) {
        net_info_ = info;
    }

    int64_t RelayWsClient::GetQueuingMsgCount() {
        if (!IsAlive()) {
            return 0;
        }
        return std::max((int64_t)queuing_msg_count_, (int64_t)client_->get_pending_event_count());
    }

    void RelayWsClient::PostNetTask(std::function<void ()> &&task) {
        if (IsAlive()) {
            client_->post_queued_event([t = std::move(task)]() {
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
