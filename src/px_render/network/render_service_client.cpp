//
// Created by RGAA on 2024-04-20.
//

#include "render_service_client.h"

#include <algorithm>
#include <px_common_new/string_util.h>

#include "rd_context.h"
#include "rd_statistics.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/async_mailbox.h"
#include "px_common_new/async_scope_drain.h"
#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/async_operation.h"
#include "px_common_new/connection_attempt_workflow.h"
#include "px_common_new/reconnect_backoff.h"
#include "px_common_new/virtual_display_timeouts.h"
#include "rd_app.h"
#include "app/app_messages.h"
#include "px_service_message.pb.h"
#include "settings/rd_settings.h"
#include "session/logical_session_registry.h"
#include <nlohmann/json.hpp>

namespace px
{

    const int kMaxClientQueuedMessage = 4096;

    namespace {

        constexpr auto kTicketRedemptionTimeout = std::chrono::seconds(5);
        constexpr auto kRenderServiceConnectionTimeout = std::chrono::seconds(10);
        constexpr std::size_t kIncomingServiceMessageCapacity = 1024;
        const PxReconnectBackoffOptions kRenderServiceReconnectOptions{
            .initial_delay = std::chrono::milliseconds(250),
            .maximum_delay = std::chrono::seconds(30),
            .multiplier = 2.0,
            .jitter_ratio = 0.2,
        };

        std::string LogicalTransportName(const LogicalSessionTransport transport) {
            switch (transport) {
            case LogicalSessionTransport::kWs: return "ws";
            case LogicalSessionTransport::kRtcLocal: return "rtc_local";
            case LogicalSessionTransport::kRtc: return "rtc";
            case LogicalSessionTransport::kUdp: return "udp";
            case LogicalSessionTransport::kFileTransfer: return "file_transfer";
            }
            return "unknown";
        }

        std::string BuildLogicalSessionsJson(
            const std::shared_ptr<LogicalSessionRegistry>& registry) {
            nlohmann::json sessions = nlohmann::json::array();
            if (!registry) {
                return sessions.dump();
            }
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            for (const auto& snapshot : registry->SnapshotActive(now_ms)) {
                nlohmann::json transports = nlohmann::json::array();
                for (const auto transport : snapshot.transports) {
                    transports.push_back(LogicalTransportName(transport));
                }
                sessions.push_back({
                    {"logical_session_id", snapshot.logical_session_id},
                    {"takeover_previous_session_id", snapshot.takeover_previous_session_id},
                    {"stream_id", snapshot.stream_id},
                    {"subject_id", snapshot.subject_id},
                    {"role", snapshot.role == LogicalSessionRole::kController
                        ? "controller" : "observer"},
                    {"transports", std::move(transports)},
                });
            }
            return sessions.dump();
        }

        template<typename T>
        PxAwaitable<PxResult<T>> ReadyAsyncResult(PxResult<T> result) {
            co_return std::move(result);
        }

        template<typename T>
        PxAwaitable<PxResult<T>> WaitForRegisteredRequest(
            std::shared_ptr<PxAsyncRequestRegistry<T>> registry,
            std::string request_id,
            std::shared_ptr<PxAsyncOneShot<T>> operation,
            std::chrono::steady_clock::time_point deadline) {
            auto result = co_await PxAsyncOneShot<T>::WaitUntil(operation, deadline);
            static_cast<void>(registry->RemoveIf(request_id, operation));
            co_return result;
        }

        std::chrono::steady_clock::duration VirtualDisplayResponseTimeout(int operation) {
            switch (operation) {
            case kVirtualDisplayQuery:
                return kVirtualDisplayQueryRenderTimeout;
            case kVirtualDisplayResetOwned:
                return kVirtualDisplayResetRenderTimeout;
            case kVirtualDisplayCreate:
            case kVirtualDisplayRemoveLast:
            default:
                return kVirtualDisplayMutationRenderTimeout;
            }
        }

        using TicketCallback = std::function<void(
            bool,
            const std::string&,
            const std::vector<std::string>&,
            const std::string&,
            const std::string&,
            const std::string&,
            const std::string&,
            const std::string&,
            int64_t,
            bool,
            bool)>;

        PxAwaitable<void> CompleteLegacyTicketRequest(
            std::weak_ptr<RenderServiceClient> weak_client,
            std::string ticket,
            std::string client_nonce,
            std::string instance_id,
            std::shared_ptr<TicketCallback> callback) {
            auto client = weak_client.lock();
            if (!client) {
                (*callback)(false, "SERVICE_STOPPED", {}, {}, {}, {}, {}, {}, 0, true, true);
                co_return;
            }
            auto request = client->RedeemConnectionTicketAsync(
                std::move(ticket),
                std::move(client_nonce),
                std::move(instance_id),
                std::chrono::steady_clock::now() + kTicketRedemptionTimeout);
            client.reset();
            auto result = co_await std::move(request);
            if (result.HasValue()) {
                auto value = result.TakeValue();
                (*callback)(true, {}, value.permissions, value.rtc_ice_config_json,
                            value.logical_session_id, value.stream_id, value.join_mode,
                            value.subject_id, value.expires_at_ms,
                            value.allow_observer, value.allow_takeover);
            }
            else {
                const auto& error = result.Error();
                (*callback)(false, error.StableCode(), {}, {}, {}, {}, {}, {}, 0, true, true);
            }
            co_return;
        }

        using VirtualDisplayCallback =
            std::function<void(const MsgVirtualDisplayServiceResult&)>;

        PxAwaitable<void> CompleteLegacyVirtualDisplayRequest(
            std::weak_ptr<RenderServiceClient> weak_client,
            std::string request_id,
            int operation,
            uint32_t width,
            uint32_t height,
            uint32_t refresh_hz,
            std::shared_ptr<VirtualDisplayCallback> callback) {
            auto client = weak_client.lock();
            if (!client) {
                MsgVirtualDisplayServiceResult stopped;
                stopped.request_id_ = std::move(request_id);
                stopped.error_code_ = "SERVICE_STOPPED";
                stopped.error_message_ = "Render Service client was destroyed";
                (*callback)(stopped);
                co_return;
            }
            auto request = client->RequestVirtualDisplayAsync(
                request_id,
                operation,
                width,
                height,
                refresh_hz,
                std::chrono::steady_clock::now() + VirtualDisplayResponseTimeout(operation));
            client.reset();
            auto result = co_await std::move(request);
            if (result.HasValue()) {
                (*callback)(result.Value());
            }
            else {
                MsgVirtualDisplayServiceResult legacy_result;
                legacy_result.request_id_ = std::move(request_id);
                legacy_result.error_code_ = result.Error().StableCode();
                legacy_result.error_message_ = result.Error().message;
                (*callback)(legacy_result);
            }
            co_return;
        }

    } // namespace

    RenderServiceClient::RenderServiceClient(const std::shared_ptr<RdApplication>& app) {
        statistics_ = RdStatistics::Instance();
        app_ = app;
        context_ = app_->GetContext();
    }

    RenderServiceClient::~RenderServiceClient() {
        Exit();
    }

    void RenderServiceClient::Start() {
        if (started_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        exiting_.store(false, std::memory_order_release);
        const auto async_runtime = context_ ? context_->GetAsyncRuntime() : std::shared_ptr<PxAsyncRuntime>{};
        if (!async_runtime || async_runtime->IsStopping()) {
            LOGE("event=module.start component=render_service code=ASYNC_RUNTIME_UNAVAILABLE "
                 "operation=start_client outcome=failed recoverable=false");
            Exit();
            return;
        }
        async_scope_ = PxAsyncScope::Create(async_runtime, PxAsyncLane::kState);
        if (async_scope_) {
            rpc_state_ = std::make_shared<RenderServiceRpcState>(async_scope_->Executor());
            incoming_messages_ = PxAsyncMailbox<std::string>::Create(async_scope_->Executor(), kIncomingServiceMessageCapacity);
        }
        connection_workflow_ = PxConnectionAttemptWorkflow::Create(async_runtime, kRenderServiceConnectionTimeout);
        connection_backoff_ = PxReconnectBackoff::Create(kRenderServiceReconnectOptions);
        if (!async_scope_ || !rpc_state_ || !incoming_messages_ || !connection_workflow_ || !connection_backoff_) {
            LOGE("event=module.start component=render_service code=ASYNC_WORKFLOW_CREATE_FAILED "
                 "operation=start_client outcome=failed recoverable=false");
            Exit();
            return;
        }
        auto weak_self = weak_from_this();
        if (!async_scope_->Spawn("render-service-receive-loop", [weak_self, mailbox = incoming_messages_]() {
                return RunIncomingMessageLoop(weak_self, mailbox);
            })) {
            LOGE("event=module.start component=render_service code=ASYNC_SCOPE_SPAWN_FAILED "
                 "operation=start_receive_loop outcome=failed recoverable=false");
            Exit();
            return;
        }
        msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kState);
        msg_listener_->Listen<MsgTimer1000>([weak_self](const MsgTimer1000&) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            self->HeartBeat();
        });

        client_ = std::make_shared<asio2::ws_client>();
        client_->set_auto_reconnect(false);
        client_->keep_alive(true);
        client_->set_timeout(std::chrono::milliseconds(2000));

        client_->bind_init([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_ || !self->connection_workflow_) {
                return;
            }
            self->websocket_upgraded_ = false;
            self->client_->ws_stream().binary(true);
            self->client_->set_no_delay(true);
            const auto ipc_token = RdSettings::Instance()->service_ipc_token_;
            self->client_->ws_stream().set_option(
                websocket::stream_base::decorator([ipc_token](websocket::request_type &req) {
                    req.set(http::field::authorization, "Bearer " + ipc_token);}
                )
            );
        })
        .bind_connect([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->client_ || !self->context_
                || !self->connection_workflow_) {
                return;
            }
            if (asio2::get_last_error()) {
                auto wstr = StringUtil::ToWString(asio2::last_error_msg());
                auto str = StringUtil::ToUTF8(wstr);
                LOGE("event=transport.connection_attempt component=render_service code={} operation=connect outcome=failure "
                     "recoverable=true reason={}", asio2::last_error_val(), str);
                const auto generation = self->connection_generation_.load(std::memory_order_acquire);
                static_cast<void>(self->connection_workflow_->FailActive(
                    generation, MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "render-service.connect", str, true)));
                return;
            }
            LOGI("RenderServiceClient, tcp connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
        })
        .bind_disconnect([weak_self]() {
            if (auto self = weak_self.lock()) {
                self->websocket_upgraded_ = false;
                if (self->connection_workflow_) {
                    const auto generation = self->connection_generation_.load(std::memory_order_acquire);
                    static_cast<void>(self->connection_workflow_->MarkDisconnected(
                        generation,
                        MakePxAsyncError(
                            PxAsyncErrorCode::kServiceNotConnected, "render-service.disconnect", "Render disconnected from Service", true)));
                }
                self->FailPendingRequests(MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected,
                    "service_websocket",
                    "Render disconnected from Service",
                    true));
            }
        })
        .bind_upgrade([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_ || !self->context_ || !self->connection_workflow_) {
                return;
            }
            if (asio2::get_last_error()) {
                LOGE("event=transport.connection_attempt component=render_service code={} operation=upgrade outcome=failure "
                     "recoverable=true reason={}", asio2::last_error_val(), asio2::last_error_msg());
                const auto generation = self->connection_generation_.load(std::memory_order_acquire);
                static_cast<void>(self->connection_workflow_->FailActive(
                    generation,
                    MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "render-service.upgrade", asio2::last_error_msg(), true)));
                return;
            }
            self->websocket_upgraded_ = true;
            LOGI("RenderServiceClient, websocket upgrade success");
            static_cast<void>(
                self->connection_workflow_->MarkReady(self->connection_generation_.load(std::memory_order_acquire)));
        })
        .bind_recv([weak_self](std::string_view data) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            const auto mailbox = self->incoming_messages_;
            if (!mailbox) {
                return;
            }
            const auto published = mailbox->TryPush(std::string(data));
            if (!published) {
                LOGE("Render Service receive mailbox rejected message: code={}, depth={}",
                     published.Error().StableCode(), mailbox->Statistics().depth);
            }
        });

        auto settings = RdSettings::Instance();
        LOGI("Will connect to service : {}:{}", settings->service_server_host_, settings->service_server_port_);
        if (!async_scope_->Spawn("render-service-connection-loop",
                                 [weak_self, workflow = connection_workflow_, backoff = connection_backoff_, client = client_,
                                  host = settings->service_server_host_, port = settings->service_server_port_]() {
            return RunConnectionLoop(weak_self, workflow, backoff, client, std::move(host), port, "/service/message?from=render");
        })) {
            LOGE("event=module.start component=render_service code=ASYNC_SCOPE_SPAWN_FAILED "
                 "operation=start_connection_loop outcome=failed recoverable=false");
            Exit();
        }
    }

    PxAwaitable<void> RenderServiceClient::RunIncomingMessageLoop(
        std::weak_ptr<RenderServiceClient> weak_client,
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
            self->ParseMessage(message.Value());
        }
    }

    PxAwaitable<void> RenderServiceClient::RunConnectionLoop(
        std::weak_ptr<RenderServiceClient> weak_client,
        std::shared_ptr<PxConnectionAttemptWorkflow> workflow,
        std::shared_ptr<PxReconnectBackoff> backoff,
        std::shared_ptr<asio2::ws_client> client,
        std::string host,
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

            LOGI("event=transport.connection_attempt component=render_service generation={}", ticket.generation);
            if (!client->async_start(host, port, path)) {
                static_cast<void>(workflow->FailActive(
                    ticket.generation,
                    MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "render-service.start", asio2::last_error_msg(), true)));
            }

            const auto ready = co_await PxConnectionAttemptWorkflow::WaitUntilReady(
                workflow, ticket, std::chrono::steady_clock::now() + kRenderServiceConnectionTimeout);
            if (ready) {
                backoff->Reset();
                LOGI("event=transport.connection_ready component=render_service generation={}", ticket.generation);
                if (self = weak_client.lock(); self && !self->exiting_.load(std::memory_order_acquire) && self->context_) {
                    self->SendPendingAppInstanceReady();
                    self->context_->SendAppMessage(MsgRenderConnected2Service{});
                }
                self.reset();
                const auto disconnected = co_await PxConnectionAttemptWorkflow::WaitUntilDisconnected(workflow, ticket);
                if (!disconnected) {
                    if (disconnected.Error().code == PxAsyncErrorCode::kServiceStopped ||
                        disconnected.Error().code == PxAsyncErrorCode::kCancelled) {
                        co_return;
                    }
                    LOGW("event=transport.connection_lost component=render_service generation={} stage={} code={} recoverable={}",
                         ticket.generation, disconnected.Error().stage, disconnected.Error().StableCode(),
                         disconnected.Error().retryable);
                } else {
                    const auto& reason = disconnected.Value().reason;
                    LOGW("event=transport.connection_lost component=render_service generation={} stage={} code={} recoverable={}",
                         ticket.generation, reason.stage, reason.StableCode(), reason.retryable);
                }
            } else if (ready.Error().code == PxAsyncErrorCode::kServiceStopped || ready.Error().code == PxAsyncErrorCode::kCancelled) {
                co_return;
            } else {
                LOGW("event=transport.connection_lost component=render_service generation={} stage={} code={} recoverable={}",
                     ticket.generation, ready.Error().stage, ready.Error().StableCode(), ready.Error().retryable);
            }

            const auto step = backoff->Next();
            LOGI("event=transport.reconnect_wait component=render_service generation={} attempt={} delay_ms={}",
                 ticket.generation, step.attempt, step.delay.count());
            const auto waited = co_await PxReconnectBackoff::Wait(step.delay);
            if (!waited) {
                co_return;
            }
        }
    }

    void RenderServiceClient::ParseMessage(const std::string& msg) {
        px::ServiceMessage sm;
        try {
            if (!sm.ParseFromString(msg)) {
                LOGE("RenderServiceClient received an invalid Service protobuf message");
                FailPendingRequests(MakePxAsyncError(
                    PxAsyncErrorCode::kProtocolError,
                    "parse_service_message",
                    "Service returned an invalid protobuf message"));
                return;
            }
        }
        catch(...) {
            LOGE("RenderServiceClient failed to parse a Service protobuf message");
            FailPendingRequests(MakePxAsyncError(
                PxAsyncErrorCode::kProtocolError,
                "parse_service_message",
                "Service response parsing threw an exception"));
            return;
        }

        if (sm.type() == ServiceMessageType::kSrvHeartBeatResp) {
            auto sub = sm.heart_beat_resp();
            auto hb_idx = sub.index();
            auto is_render_alive = sub.render_status() == RenderStatus::kWorking;
            //LOGI("hb_idx: {}, is render alive: {}", hb_idx, is_render_alive);
        }
        else if (sm.type() == ServiceMessageType::kSrvStopServer) {
            // Console stopped this instance: notify clients then exit gracefully
            LOGW("kSrvStopServer received from service, stopping render...");
            app_->OnServiceRequestedStop();
        }
        else if (sm.type() == ServiceMessageType::kSrvRedeemConnectionTicketResp) {
            const auto& sub = sm.redeem_connection_ticket_resp();
            LOGI("Received connection ticket redemption response: ok={}, grant_present={}, grant_permission_count={}",
                 sub.ok(), sub.has_grant(),
                 sub.has_grant() ? sub.grant().permissions_size() : 0);
            PxResult<RedeemedConnectionTicket> result = [&sub]() {
                if (!sub.ok()) {
                    return PxResult<RedeemedConnectionTicket>::Failure(MakePxAsyncError(
                        PxAsyncErrorCode::kServiceRejected,
                        "redeem_ticket",
                        "Service rejected the connection ticket",
                        false,
                        sub.code()));
                }
                RedeemedConnectionTicket ticket;
                if (sub.has_grant()) {
                    ticket.permissions.assign(
                        sub.grant().permissions().begin(),
                        sub.grant().permissions().end());
                    ticket.logical_session_id = sub.grant().logical_session_id();
                    ticket.stream_id = sub.grant().stream_id();
                    ticket.join_mode = sub.grant().join_mode();
                    ticket.subject_id = sub.grant().subject_id();
                    ticket.expires_at_ms = sub.grant().expires_at();
                    ticket.allow_observer = sub.grant().allow_observer();
                    ticket.allow_takeover = sub.grant().allow_takeover();
                }
                ticket.rtc_ice_config_json = sub.rtc_ice_config_json();
                return PxResult<RedeemedConnectionTicket>::Success(std::move(ticket));
            }();
            if (!rpc_state_ || !rpc_state_->ticket_requests_->Complete(
                    sub.request_id(), std::move(result))) {
                LOGW("Ignore late or unknown ticket response: request_id={}", sub.request_id());
            }
        }
        else if (sm.type() == ServiceMessageType::kSrvVirtualDisplayResult) {
            const auto& sub = sm.virtual_display_result();
            MsgVirtualDisplayServiceResult result;
            result.request_id_ = sub.request_id();
            result.accepted_ = sub.accepted();
            result.topology_changed_ = sub.topology_changed();
            result.topology_generation_ = sub.topology_generation();
            result.logical_display_id_ = sub.logical_display_id();
            result.error_code_ = sub.error_code();
            result.error_message_ = sub.error_message();
            result.owned_display_count_ = sub.owned_display_count();
            result.actual_virtual_display_count_ = sub.actual_virtual_display_count();
            result.driver_installed_ = sub.driver_installed();
            result.package_valid_ = sub.package_valid();
            result.removal_safe_ = sub.removal_safe();
            result.phase_ = sub.phase();

            if (!rpc_state_ || !rpc_state_->virtual_display_requests_->Complete(
                    result.request_id_,
                    PxResult<MsgVirtualDisplayServiceResult>::Success(result))) {
                LOGW("Ignore late or unknown virtual display response: request_id={}",
                     result.request_id_);
            }
            if (context_) {
                context_->PostTask([weak_self = weak_from_this(), result]() {
                    const auto self = weak_self.lock();
                    if (self && self->context_) {
                        self->context_->SendAppMessage(result);
                    }
                });
            }
        }
    }

    void RenderServiceClient::Exit() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        const auto scope = BeginStop();
        if (!scope) {
            FinishStop();
            return;
        }
        if (scope->IsScopeThread()) {
            LOGI("event=async.scope_drain component=render_service operation=stop_client outcome=deferred "
                 "reason=shutdown_requested_from_runtime_thread outstanding={}",
                 scope->GetStatistics().outstanding);
            return;
        }
        const auto client = client_;
        const auto adapter_stopped = WaitForAsioClientStoppedBlocking(client, deadline);
        const auto remaining = std::max(
            std::chrono::milliseconds::zero(),
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
        if (!adapter_stopped || !scope->WaitFor(remaining)) {
            LOGE("event=async.scope_drain component=render_service code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                 "operation=stop_client outcome=timeout recoverable=false outstanding={}",
                 scope->GetStatistics().outstanding);
            return;
        }
        FinishStop();
    }

    PxAwaitable<PxResult<void>> RenderServiceClient::StopAsync(
        const std::shared_ptr<RenderServiceClient>& owner,
        const std::chrono::steady_clock::time_point deadline) {
        if (!owner) {
            co_return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kInvalidArgument, "render-service.stop", "Render Service client owner is missing"));
        }
        const auto scope = owner->BeginStop();
        if (scope) {
            const auto adapter_stopped = co_await WaitForAsioClientStopped(owner->client_, deadline, "render-service.adapter-stop");
            if (!adapter_stopped) {
                co_return adapter_stopped;
            }
            const auto drained = co_await WaitForAsyncScopeDrain(scope, deadline, "render-service.stop");
            if (!drained) {
                LOGE("event=async.scope_drain component=render_service code={} operation=stop_client "
                     "outcome=failed recoverable={} outstanding={} reason={}",
                     drained.Error().StableCode(),
                     drained.Error().retryable,
                     scope->GetStatistics().outstanding,
                     drained.Error().message);
                co_return PxResult<void>::Failure(drained.Error());
            }
        }
        owner->FinishStop();
        co_return PxResult<void>::Success();
    }

    std::shared_ptr<PxAsyncScope> RenderServiceClient::BeginStop() {
        if (exiting_.exchange(true)) {
            return async_scope_;
        }
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        FailPendingRequests(MakePxAsyncError(
            PxAsyncErrorCode::kServiceStopped,
            "service_shutdown",
            "Render Service client is stopping"));
        if (incoming_messages_) {
            static_cast<void>(incoming_messages_->Close(MakePxAsyncError(
                PxAsyncErrorCode::kServiceStopped, "render-service.receive", "Render Service client is stopping")));
        }
        if (connection_workflow_) {
            connection_workflow_->Stop();
        }
        const auto client = client_;
        static_cast<void>(RequestAsioClientStop(client, "render-service.adapter-stop"));
        if (async_scope_) {
            async_scope_->BeginStop();
        }
        return async_scope_;
    }

    void RenderServiceClient::FinishStop() {
        if (async_scope_ && async_scope_->GetStatistics().outstanding != 0) {
            return;
        }
        client_.reset();
        incoming_messages_.reset();
        connection_workflow_.reset();
        connection_backoff_.reset();
        rpc_state_.reset();
        async_scope_.reset();
        websocket_upgraded_.store(false, std::memory_order_release);
        started_.store(false, std::memory_order_release);
    }

    bool RenderServiceClient::IsAlive() const {
        return client_ != nullptr && client_->is_started()
            && websocket_upgraded_.load(std::memory_order_acquire)
            && connection_workflow_ && connection_workflow_->IsReady();
    }

    void RenderServiceClient::HeartBeat() {
        static int64_t hb_idx = 0;
        px::ServiceMessage msg;
        msg.set_type(ServiceMessageType::kSrvHeartBeat);
        auto sub = msg.mutable_heart_beat();
        sub->set_index(hb_idx++);
        sub->set_from(std::format("render_{}", RdSettings::Instance()->transmission_.listening_port_));
        sub->set_logical_sessions_json(BuildLogicalSessionsJson(
            app_ ? app_->GetLogicalSessionRegistry() : std::shared_ptr<LogicalSessionRegistry>{}));
        PostNetMessage(msg.SerializeAsString());
    }

    void RenderServiceClient::PostNetMessage(const std::string& msg) {
        const auto result = TryPostNetMessage(msg);
        if (!result.HasValue()) {
            LOGW("RenderServiceClient rejected outgoing message: code={}, stage={}, reason={}",
                 result.Error().StableCode(), result.Error().stage, result.Error().message);
        }
    }

    PxResult<void> RenderServiceClient::TryPostNetMessage(const std::string& msg) {
        const auto client = client_;
        if (exiting_.load(std::memory_order_acquire)) {
            return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kServiceStopped,
                "queue_service_message",
                "Render Service client is stopping"));
        }
        if (!client || !client->is_started() ||
            !websocket_upgraded_.load(std::memory_order_acquire)) {
            return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kServiceNotConnected,
                "queue_service_message",
                "Render is not connected to Service",
                true));
        }

        const auto previous = queuing_message_count_.fetch_add(1, std::memory_order_acq_rel);
        if (previous >= kMaxClientQueuedMessage) {
            queuing_message_count_.fetch_sub(1, std::memory_order_acq_rel);
            return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kQueueFull,
                "queue_service_message",
                "Render Service message queue is full",
                true));
        }

        const auto weak_self = weak_from_this();
        client->async_send(msg, [weak_self]() {
            const auto send_error = asio2::get_last_error();
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->queuing_message_count_.fetch_sub(1, std::memory_order_acq_rel);
            if (send_error) {
                self->FailPendingRequests(MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected,
                    "send_service_message",
                    "failed to send a message to Service",
                    true,
                    std::to_string(send_error.value())));
            }
        });
        return PxResult<void>::Success();
    }

    void RenderServiceClient::FailPendingRequests(const PxAsyncError& error) {
        if (!rpc_state_) {
            return;
        }
        const auto ticket_count = rpc_state_->ticket_requests_->FailAll(error);
        const auto display_count = rpc_state_->virtual_display_requests_->FailAll(error);
        if (ticket_count != 0 || display_count != 0) {
            LOGW("Render Service pending requests failed: tickets={}, virtual_displays={}, code={}",
                 ticket_count, display_count, error.StableCode());
        }
    }

    void RenderServiceClient::NotifyAppInstanceReady(
        const std::string& instance_id, int listen_port, bool ok, const std::string& error) {
        {
            std::scoped_lock lock(ready_mtx_);
            ready_instance_id_ = instance_id;
            ready_listen_port_ = listen_port;
            ready_ok_ = ok;
            ready_error_ = error;
            ready_pending_ = true;
        }
        SendPendingAppInstanceReady();
    }

    void RenderServiceClient::SendPendingAppInstanceReady() {
        if (!websocket_upgraded_) return;
        px::ServiceMessage message;
        {
            std::scoped_lock lock(ready_mtx_);
            if (!ready_pending_) return;
            message.set_type(ServiceMessageType::kSrvAppInstanceReady);
            auto* ready = message.mutable_app_instance_ready();
            ready->set_instance_id(ready_instance_id_);
            ready->set_listen_port(ready_listen_port_);
            ready->set_ok(ready_ok_);
            ready->set_error(ready_error_);
            ready_pending_ = false;
        }
        PostNetMessage(message.SerializeAsString());
    }

    void RenderServiceClient::RedeemConnectionTicket(
        const std::string& ticket,
        const std::string& client_nonce,
        const std::string& instance_id,
        std::function<void(bool, const std::string&, const std::vector<std::string>&,
                           const std::string&, const std::string&, const std::string&,
                           const std::string&, const std::string&, int64_t,
                           bool, bool)>&& callback) {
        if (!callback) {
            return;
        }
        if (!async_scope_ || !async_scope_->IsAccepting()) {
            callback(false, "SERVICE_STOPPED", {}, "", "", "", "", "", 0, true, true);
            return;
        }
        const auto callback_state = std::make_shared<TicketCallback>(std::move(callback));
        const auto weak_self = weak_from_this();
        if (!async_scope_->Spawn("redeem-connection-ticket",
                [weak_self, ticket, client_nonce, instance_id, callback_state]() {
                    return CompleteLegacyTicketRequest(
                        weak_self, ticket, client_nonce, instance_id, callback_state);
                })) {
            (*callback_state)(false, "SERVICE_STOPPED", {}, "", "", "", "", "", 0, true, true);
        }
    }

    PxAwaitable<PxResult<RedeemedConnectionTicket>>
    RenderServiceClient::RedeemConnectionTicketAsync(
        std::string ticket,
        std::string client_nonce,
        std::string instance_id,
        std::chrono::steady_clock::time_point deadline) {
        if (ticket.empty() || client_nonce.empty()) {
            return ReadyAsyncResult(PxResult<RedeemedConnectionTicket>::Failure(
                MakePxAsyncError(
                    PxAsyncErrorCode::kInvalidArgument,
                    "redeem_ticket",
                    "ticket and client nonce are required")));
        }
        if (!IsAlive() || !websocket_upgraded_.load(std::memory_order_acquire) || !rpc_state_) {
            return ReadyAsyncResult(PxResult<RedeemedConnectionTicket>::Failure(
                MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected,
                    "redeem_ticket",
                    "Render is not connected to Service",
                    true)));
        }
        // The Direct RTC signaling flow may legitimately redeem the same ticket
        // twice: the first allocation reports kOccupied and the second retries
        // with takeover=1.  Keep the redemption id stable for that logical
        // connection so Console can treat only that exact retry as idempotent.
        // The ticket itself is never put in logs or on the wire as an id.
        const auto redemption_fingerprint = MD5::Hex(
            ticket + "\n" + client_nonce + "\n" + instance_id);
        const auto request_id = std::format(
            "render-{}-{}",
            RdSettings::Instance()->transmission_.listening_port_,
            redemption_fingerprint);
        auto registered = rpc_state_->ticket_requests_->Register(request_id);
        if (!registered.HasValue()) {
            return ReadyAsyncResult(PxResult<RedeemedConnectionTicket>::Failure(
                registered.Error()));
        }
        const auto operation = registered.Value();
        px::ServiceMessage message;
        message.set_type(ServiceMessageType::kSrvRedeemConnectionTicket);
        auto* request = message.mutable_redeem_connection_ticket();
        request->set_request_id(request_id);
        request->set_ticket(ticket);
        request->set_client_nonce(client_nonce);
        request->set_instance_id(instance_id);
        const auto send_result = TryPostNetMessage(message.SerializeAsString());
        if (!send_result.HasValue()) {
            static_cast<void>(rpc_state_->ticket_requests_->Complete(
                request_id,
                PxResult<RedeemedConnectionTicket>::Failure(send_result.Error())));
        }
        return WaitForRegisteredRequest(
            rpc_state_->ticket_requests_, request_id, operation, deadline);
    }

    void RenderServiceClient::RequestVirtualDisplay(
        const std::string& request_id,
        int operation,
        uint32_t width,
        uint32_t height,
        uint32_t refresh_hz,
        std::function<void(const MsgVirtualDisplayServiceResult&)>&& callback) {
        if (!callback) {
            return;
        }
        if (!async_scope_ || !async_scope_->IsAccepting()) {
            MsgVirtualDisplayServiceResult result;
            result.request_id_ = request_id;
            result.error_code_ = "SERVICE_STOPPED";
            result.error_message_ = "Render Service client is stopping";
            callback(result);
            return;
        }
        const auto callback_state =
            std::make_shared<VirtualDisplayCallback>(std::move(callback));
        const auto weak_self = weak_from_this();
        if (!async_scope_->Spawn("virtual-display-service-request",
                [weak_self, request_id, operation, width, height, refresh_hz, callback_state]() {
                    return CompleteLegacyVirtualDisplayRequest(
                        weak_self, request_id, operation, width, height, refresh_hz, callback_state);
                })) {
            MsgVirtualDisplayServiceResult result;
            result.request_id_ = request_id;
            result.error_code_ = "SERVICE_STOPPED";
            result.error_message_ = "Render Service async scope rejected the request";
            (*callback_state)(result);
        }
    }

    PxAwaitable<PxResult<MsgVirtualDisplayServiceResult>>
    RenderServiceClient::RequestVirtualDisplayAsync(
        std::string request_id,
        int operation,
        uint32_t width,
        uint32_t height,
        uint32_t refresh_hz,
        std::chrono::steady_clock::time_point deadline) {
        if (request_id.empty() || operation < kVirtualDisplayCreate ||
            operation > kVirtualDisplayResetOwned) {
            return ReadyAsyncResult(PxResult<MsgVirtualDisplayServiceResult>::Failure(
                MakePxAsyncError(
                    PxAsyncErrorCode::kInvalidArgument,
                    "virtual_display",
                    "virtual display request is invalid")));
        }
        if (!IsAlive() || !websocket_upgraded_.load(std::memory_order_acquire) || !rpc_state_) {
            return ReadyAsyncResult(PxResult<MsgVirtualDisplayServiceResult>::Failure(
                MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected,
                    "virtual_display",
                    "Render is not connected to Service",
                    true)));
        }
        auto registered = rpc_state_->virtual_display_requests_->Register(request_id);
        if (!registered.HasValue()) {
            return ReadyAsyncResult(PxResult<MsgVirtualDisplayServiceResult>::Failure(
                registered.Error()));
        }
        const auto request_operation = registered.Value();

        px::ServiceMessage message;
        message.set_type(ServiceMessageType::kSrvVirtualDisplayRequest);
        auto* request = message.mutable_virtual_display_request();
        request->set_request_id(request_id);
        request->set_operation(static_cast<VirtualDisplayOperation>(operation));
        request->set_width(width);
        request->set_height(height);
        request->set_refresh_hz(refresh_hz);
        const auto send_result = TryPostNetMessage(message.SerializeAsString());
        if (!send_result.HasValue()) {
            static_cast<void>(rpc_state_->virtual_display_requests_->Complete(
                request_id,
                PxResult<MsgVirtualDisplayServiceResult>::Failure(send_result.Error())));
        }
        return WaitForRegisteredRequest(
            rpc_state_->virtual_display_requests_, request_id, request_operation, deadline);
    }

}
