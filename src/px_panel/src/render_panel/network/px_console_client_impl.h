//
// Created by RGAA on 17/05/2025.
//

#ifndef PX_PANEL_CONSOLE_CLIENT_IMPL_H
#define PX_PANEL_CONSOLE_CLIENT_IMPL_H

#include <memory>
#include <type_traits>
#include <asio2/websocket/ws_client.hpp>
#include <asio2/websocket/wss_client.hpp>
#include "px_console_client.h"
#include "px_common_new/concurrent_type.h"
#include "record_transfer.h"

#include "console_panel.pb.h"
#include "px_common_new/log.h"
#include "render_panel/px_context.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/task_runtime.h"
#include "render_panel/px_app_messages.h"
#include "px_common_new/time_util.h"
#include "render_panel/px_settings.h"
#include "px_common_new/base64.h"
#include "px_common_new/http_client.h"
#include "records_catalog.h"
#include "records_http_handler.h"
#include "hw_info/hw_info.h"
#include <nlohmann/json.hpp>
#include "render_panel/px_application.h"
#include "render_panel/user/px_user_manager.h"
#include "network/ct_auth_token.h"
#include "panel_rtc_config_refresh_gate.h"
#include "px_common_new/async_operation.h"
#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/reconnect_supervisor.h"
#include "px_common_new/websocket_reconnect_adapter.h"

#include <chrono>
#include <filesystem>
#include <mutex>

using namespace console_panel;

namespace px
{
    namespace fs = std::filesystem;

    struct PanelRtcConfigHttpResponse {
        int status = 0;
        std::string body;
    };

    // loopback / link-local addresses are useless for lan-direct access (design 5.2)
    static bool IsUsableLanIp(const std::string& ip) {
        return !ip.empty()
            && ip.rfind("127.", 0) != 0
            && ip.rfind("169.254.", 0) != 0;
    }

    // ClientType: asio2::wss_client(ssl) or asio2::ws_client(plain),
    // both share the same websocket client interface.
    template<typename ClientType>
    class PxConsoleClientImpl : public PxConsoleClient,
                            public std::enable_shared_from_this<PxConsoleClientImpl<ClientType>> {
    public:
        explicit PxConsoleClientImpl(const std::shared_ptr<PxContext>& ctx,
                                 const std::string& host,
                                 int port,
                                 const std::string& device_id) {
            context_ = ctx;
            host_ = host;
            port_ = port;
            device_id_ = device_id;
        }

        ~PxConsoleClientImpl() override {
            Stop();
        }

        void Start() override {
            std::unique_lock operation_lock(operation_mutex_, std::try_to_lock);
            if (!operation_lock.owns_lock()) {
                return;
            }
            if (stopping_) {
                return;
            }
            {
                std::lock_guard lock(network_mutex_);
                if (client_) {
                    return;
                }
            }
            auto weak_self = this->weak_from_this();

            msg_listener_ = context_->ObtainMessageListener(MessageExecutionLane::kState);
            msg_listener_->Listen<MsgGrTimer1S>([weak_self](const MsgGrTimer1S& m) {
                auto self = weak_self.lock();
                if (!self || !self->context_) {
                    return;
                }
                self->context_->PostTask([weak_self]() {
                    auto self = weak_self.lock();
                    if (!self) {
                        return;
                    }
                    self->Heartbeat();
                });
            });

            msg_listener_->Listen<MsgHWInfo>([weak_self](const MsgHWInfo& info) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                self->sys_info_ = info.sys_info_;
            });

            if (const auto notifier = context_->GetMessageNotifier()) {
                if (const auto runtime = notifier->GetAsyncRuntime()) {
                    {
                        std::lock_guard lock(network_mutex_);
                        async_runtime_ = runtime;
                        connection_scope_ = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
                        reconnect_supervisor_ = PxReconnectSupervisor::Create(
                            runtime, MakeWebSocketReconnectOptions("panel_console"));
                    }
                    rtc_config_scope_ = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
                    record_fetch_scope_ = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
                    record_list_scope_ = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
                }
            }
            fetch_queue_ = std::make_shared<RecordFetchQueue>();
            record_fetch_blocking_runtime_ = std::make_shared<TaskRuntime>(1);
            record_list_blocking_runtime_ = std::make_shared<TaskRuntime>(1);

            if (!connection_scope_ || !reconnect_supervisor_) {
                LOGE("event=module.start component=panel_console code=ASYNC_WORKFLOW_CREATE_FAILED "
                     "operation=start_client outcome=failed recoverable=false");
                operation_lock.unlock();
                Stop();
                return;
            }

            {
                std::lock_guard lock(network_mutex_);
                client_ = std::make_shared<ClientType>();
            }
            client_->set_auto_reconnect(false);
            client_->keep_alive(true);
            client_->set_timeout(std::chrono::milliseconds(3000));
            if constexpr (std::is_same_v<ClientType, asio2::wss_client>) {
                client_->set_verify_mode(asio::ssl::verify_none);
            }

            client_->bind_init([weak_self]() {
                auto self = weak_self.lock();
                if (!self || !self->client_) {
                    return;
                }
                self->client_->ws_stream().binary(true);
                self->client_->set_no_delay(true);

                // Generate a fresh token for every connection attempt (including auto reconnect).
                // The token has a short lifetime (60s), so reusing the original path on reconnect
                // would cause the Console token filter to reject the connection.
                auto user_id = grApp->GetUserManager()->GetUserId();
                auto token = GenerateConnectionToken(grApp->GetAppkey());
                const auto endpoint = self->use_legacy_cms_path_.load()
                    ? "/cms/panel"
                    : "/console/panel";
                auto path = std::format("{}?appkey={}&token={}&ts={}&nonce={}&device_id={}&user_id={}",
                                         endpoint, grApp->GetAppkey(), token.token, token.ts, token.nonce, self->device_id_, user_id);
                self->client_->set_upgrade_target(path);
            })
            .bind_connect([weak_self]() {
                auto self = weak_self.lock();
                if (!self || !self->client_) {
                    return;
                }
                if (asio2::get_last_error()) {
                    static_cast<void>(self->reconnect_supervisor_->FailActive(self->callback_generation_.load(), MakePxAsyncError(
                        PxAsyncErrorCode::kServiceNotConnected,
                        "panel-console.connect",
                        asio2::last_error_msg(),
                        true)));
                } else {
                    LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
                }
            })
            .bind_upgrade([weak_self]() {
                if (asio2::get_last_error()) {
                    if (auto self = weak_self.lock(); self && !self->use_legacy_cms_path_.exchange(true)) {
                        LOGW("event=transport.route_fallback component=panel_console code=PRIMARY_ROUTE_UNAVAILABLE "
                             "operation=upgrade outcome=fallback recoverable=true target=/cms/panel");
                    }
                    if (auto self = weak_self.lock(); self && !self->stopping_
                        && self->reconnect_supervisor_) {
                        static_cast<void>(self->reconnect_supervisor_->FailActive(self->callback_generation_.load(), MakePxAsyncError(
                            PxAsyncErrorCode::kProtocolError,
                            "panel-console.upgrade",
                            asio2::last_error_msg(),
                            true)));
                    }
                    return;
                }
                if (auto self = weak_self.lock(); self && !self->stopping_
                    && self->reconnect_supervisor_) {
                    static_cast<void>(self->reconnect_supervisor_->MarkReady(self->callback_generation_.load()));
                }
            })
            .bind_disconnect([weak_self]() {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                if (self->reconnect_supervisor_) {
                    static_cast<void>(self->reconnect_supervisor_->MarkDisconnected(self->callback_generation_.load(), MakePxAsyncError(
                        PxAsyncErrorCode::kServiceNotConnected,
                        "panel-console.disconnect",
                        "Panel disconnected from Console",
                        true)));
                }
            })
            .bind_recv([weak_self](std::string_view data) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                auto msg = std::string(data.data(), data.size());
                self->ParseMessage(msg);
            });

            LOGI("will connect => {}:{}/console/panel", host_, port_);
            PxReconnectSupervisorHooks hooks{
                .start_attempt = [weak_self, client = client_, host = host_, port = port_](const std::uint64_t generation) {
                    const auto self = weak_self.lock();
                    if (!self || self->stopping_) {
                        return PxResult<void>::Failure(MakePxAsyncError(
                            PxAsyncErrorCode::kServiceStopped, "panel-console.start", "Panel Console owner is stopping"));
                    }
                    self->callback_generation_.store(generation, std::memory_order_release);
                    return StartWebSocketAdapter(client, host, port, "panel-console.start");
                },
                .stop_attempt = [client = client_](const std::chrono::steady_clock::time_point deadline) {
                    return StopWebSocketAdapter(client, deadline, "panel-console.retry-reset");
                },
                .on_ready = [weak_self](std::uint64_t) {
                    if (const auto self = weak_self.lock(); self && !self->stopping_) {
                        self->Hello();
                        self->RefreshRtcConfigAsync(0);
                    }
                },
            };
            if (!connection_scope_->Spawn("panel-console-reconnect", [supervisor = reconnect_supervisor_, hooks = std::move(hooks)]() mutable {
                    return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
                })) {
                LOGE("event=module.start component=panel_console code=ASYNC_SCOPE_SPAWN_FAILED "
                     "operation=start_reconnect outcome=failed recoverable=false");
                operation_lock.unlock();
                Stop();
            }

        }

        void Stop() override {
            std::unique_lock operation_lock(operation_mutex_, std::try_to_lock);
            if (!operation_lock.owns_lock()) {
                return;
            }
            const auto first_stop = !stopping_.exchange(true);
            if (first_stop && msg_listener_) {
                msg_listener_->UnListenAll();
                msg_listener_.reset();
            }
            std::shared_ptr<ClientType> client;
            std::shared_ptr<PxAsyncScope> connection_scope;
            std::shared_ptr<PxReconnectSupervisor> supervisor;
            {
                std::lock_guard lock(network_mutex_);
                client = client_;
                connection_scope = connection_scope_;
                supervisor = reconnect_supervisor_;
            }
            if (supervisor) {
                supervisor->Stop();
            }
            static_cast<void>(RequestAsioClientStop(client, "panel-console.stop"));
            if (connection_scope) {
                connection_scope->BeginStop();
            }
            if (first_stop) {
                record_list_request_gate_->Stop();
            }
            if (first_stop && record_list_scope_) {
                if (record_list_scope_->IsScopeThread()) {
                    record_list_scope_->BeginStop();
                }
                else if (!record_list_scope_->StopAndWait(std::chrono::milliseconds(2000))) {
                    LOGW("event=async.scope_drain component=panel_console code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                         "operation=stop_record_list outcome=timeout recoverable=false");
                }
                record_list_scope_.reset();
            }
            if (first_stop && record_list_blocking_runtime_) {
                record_list_blocking_runtime_->Exit();
                record_list_blocking_runtime_.reset();
            }
            if (first_stop && fetch_queue_) {
                fetch_queue_->Stop();
            }
            if (first_stop) {
                if (const auto cancellation =
                    active_record_upload_cancellation_.exchange({})) {
                    cancellation->store(true, std::memory_order_release);
                }
            }
            if (first_stop && record_fetch_scope_) {
                if (record_fetch_scope_->IsScopeThread()) {
                    record_fetch_scope_->BeginStop();
                }
                else if (!record_fetch_scope_->StopAndWait(std::chrono::milliseconds(2000))) {
                    LOGW("event=async.scope_drain component=panel_console code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                         "operation=stop_record_fetch outcome=timeout recoverable=false");
                }
                record_fetch_scope_.reset();
            }
            if (first_stop && record_fetch_blocking_runtime_) {
                record_fetch_blocking_runtime_->Exit();
                record_fetch_blocking_runtime_.reset();
            }
            if (first_stop) {
                rtc_config_refresh_gate_->Stop();
            }
            if (first_stop && rtc_config_scope_) {
                if (rtc_config_scope_->IsScopeThread()) {
                    rtc_config_scope_->BeginStop();
                }
                else if (!rtc_config_scope_->StopAndWait(std::chrono::milliseconds(2000))) {
                    LOGW("event=async.scope_drain component=panel_console code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                         "operation=stop_rtc_config outcome=timeout recoverable=false");
                }
                rtc_config_scope_.reset();
            }
            if (connection_scope && connection_scope->IsScopeThread()) {
                ScheduleDeferredStop();
                return;
            }
            const auto result = StopWebSocketConnectionBlocking(
                client, connection_scope, std::chrono::seconds(5), "panel-console.stop");
            if (!result.Succeeded()) {
                LOGE("event=async.scope_drain component=panel_console code=ASYNC_SCOPE_DRAIN_TIMEOUT operation=stop "
                     "outcome=timeout recoverable=false scope_drained={} adapter_stopped={} outstanding={}",
                     result.scope_drained, result.adapter_stopped,
                     connection_scope ? connection_scope->GetStatistics().outstanding : 0);
                return;
            }
            std::lock_guard lock(network_mutex_);
            client_.reset();
            reconnect_supervisor_.reset();
            connection_scope_.reset();
            async_runtime_.reset();
            deferred_stop_scheduled_ = false;
        }

        void ScheduleDeferredStop() {
            if (deferred_stop_scheduled_.exchange(true, std::memory_order_acq_rel)) {
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
                        self->Stop();
                    }
                })) {
                deferred_stop_scheduled_ = false;
                LOGE("event=async.scope_drain component=panel_console code=ASYNC_DEFER_FAILED operation=stop "
                     "outcome=failed recoverable=false");
            }
        }

        bool IsStarted() override {
            std::lock_guard lock(network_mutex_);
            return client_ != nullptr;
        }

        bool IsActive() override {
            std::lock_guard lock(network_mutex_);
            return !stopping_ && client_ && client_->is_started() && reconnect_supervisor_ && reconnect_supervisor_->IsReady();
        }

        void PostBinMessage(const std::string& m) override {
            std::shared_ptr<ClientType> client;
            std::shared_ptr<PxReconnectSupervisor> supervisor;
            {
                std::lock_guard lock(network_mutex_);
                client = client_;
                supervisor = reconnect_supervisor_;
            }
            if (!stopping_ && client && client->is_started() && supervisor && supervisor->IsReady()) {
                client->async_send(m);
            }
        }

        bool IsAlive() const override {
            auto current_timestamp = TimeUtil::GetCurrentTimestamp();
            auto diff = current_timestamp - last_received_timestamp_ < 3100;
            //LOGI("Diff alive: {}", diff);
            return diff;
        }

    private:
        void Hello() {
            if (!IsActive()) {
                return;
            }
            console_panel::ConsolePanelMessage msg;
            msg.set_msg_type(console_panel::ConsolePanelMessageType::kConsolePanelHello);
            auto sub = msg.mutable_hello();
            sub->set_device_id(device_id_);
            auto user_id = grApp->GetUserManager()->GetUserId();
            sub->set_user_id(user_id);
            sub->set_device_name(PxSettings::Instance()->GetDeviceName());

            // report local NIC IPv4 list for the console render-records view (design 5.2);
            // the tunnel source ip is unreliable across routers/NAT
            for (const auto& eth : context_->GetIps()) {
                if (IsUsableLanIp(eth.ip_addr_)) {
                    sub->add_panel_lan_ips(eth.ip_addr_);
                }
            }
            sub->set_panel_http_port(PxSettings::Instance()->GetPanelServerPort());
            PostBinMessage(msg.SerializeAsString());
        }

        void Heartbeat() {
            if (!IsActive()) {
                return;
            }

            auto ips = context_->GetIps();
            auto desktop_link_raw = context_->MakeDesktopLinkMessage(ips);
            auto desktop_link = std::format("link://{}", Base64::Base64Encode(desktop_link_raw));

            console_panel::ConsolePanelMessage msg;
            msg.set_msg_type(console_panel::ConsolePanelMessageType::kConsolePanelHeartBeat);
            auto sub = msg.mutable_heartbeat();
            sub->set_hb_index(hb_idx_++);
            sub->set_device_id(device_id_);
            sub->set_desktop_link(desktop_link);
            sub->set_desktop_link_raw(desktop_link_raw);
            auto user_id = grApp->GetUserManager()->GetUserId();
            sub->set_user_id(user_id);
            if (auto sys_info = sys_info_.Clone(); sys_info != nullptr) {
                try {
                    auto obj = nlohmann::json::parse(sys_info->raw_json_msg_);
                    obj["cpu"]["current_frequency"] = sys_info->cpu_.current_frequency_;
                    sub->set_sys_info_raw(obj.dump());
                }
                catch (...) {
                    sub->set_sys_info_raw(sys_info->raw_json_msg_);
                }

                //LOGI("Heartbeat sys infor raw: {}", sys_info->raw_json_msg_);
            }
            if (!ips.empty()) {
                sub->set_device_ip_addr(ips[0].ip_addr_);
            }
            sub->set_device_name(PxSettings::Instance()->GetDeviceName());
            PostBinMessage(msg.SerializeAsString());
            if ((hb_idx_.load() % 60) == 0) {
                RefreshRtcConfigAsync(0);
            }
        }

        void ParseMessage(const std::string& m) {
            auto pm = std::make_shared<console_panel::ConsolePanelMessage>();
            bool r = pm->ParsePartialFromString(m);
            if (!r) {
                LOGE("Parse ConsoleClient message failed!");
                return;
            }
            last_received_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();

            auto type = pm->msg_type();
            if (type == ConsolePanelMessageType::kConsolePanelHello) {
                LOGI("ConsoleClient hello.");
            }
            else if (type == ConsolePanelMessageType::kConsolePanelHeartBeat) {
                //LOGI("ConsoleClient heartbeat.");
            }
            else if (type == ConsolePanelMessageType::kRecordListReq) {
                if (pm->has_record_list_req()) {
                    HandleRecordListReq(pm->record_list_req());
                }
            }
            else if (type == ConsolePanelMessageType::kRecordFetchReq) {
                if (pm->has_record_fetch_req()) {
                    HandleRecordFetchReq(pm->record_fetch_req());
                }
            }
            else if (type == ConsolePanelMessageType::kRtcIceConfigChanged) {
                if (pm->has_rtc_ice_config_changed()) {
                    RefreshRtcConfigAsync(pm->rtc_ice_config_changed().revision());
                }
            }
        }

        void RefreshRtcConfigAsync(uint64_t expected_revision) {
            const auto request = rtc_config_refresh_gate_->Request(expected_revision);
            if (request != PanelRtcConfigRefreshRequest::kStarted) {
                return;
            }
            const auto scope = rtc_config_scope_;
            if (!scope) {
                rtc_config_refresh_gate_->AbortStart();
                LOGW("Panel RTC config refresh ignored because async scope is unavailable");
                return;
            }
            auto weak_self = this->weak_from_this();
            if (!scope->Spawn("panel-rtc-config-refresh", [weak_self]() {
                    return RunRtcConfigRefresh(weak_self);
                })) {
                rtc_config_refresh_gate_->AbortStart();
                LOGW("Panel RTC config refresh was rejected by the async scope");
            }
        }

        static PxAwaitable<PxResult<PanelRtcConfigHttpResponse>> FetchRtcConfig(
            std::shared_ptr<PxContext> context,
            asio::any_io_executor executor,
            std::string host,
            int port,
            std::string appkey) {
            const auto operation = PxAsyncOneShot<PanelRtcConfigHttpResponse>::Create(executor);
            context->PostNetworkTask([operation, host = std::move(host), port,
                                      appkey = std::move(appkey)]() {
                try {
                    const auto client = HttpClient::MakeSSL(
                        host, port, "/api/v1/rtc/ice-config", 5000);
                    client->SetHeader("x-px-appkey", appkey);
                    const auto response = client->Request();
                    static_cast<void>(operation->TryComplete(
                        PxResult<PanelRtcConfigHttpResponse>::Success({
                            .status = response.status,
                            .body = response.body,
                        })));
                }
                catch (const std::exception& error) {
                    static_cast<void>(operation->TryFail(MakePxAsyncError(
                        PxAsyncErrorCode::kProtocolError,
                        "panel_rtc_config_http",
                        error.what(),
                        true)));
                }
            });
            co_return co_await PxAsyncOneShot<PanelRtcConfigHttpResponse>::WaitUntil(
                operation, std::chrono::steady_clock::now() + std::chrono::seconds(6));
        }

        static PxAwaitable<void> RunRtcConfigRefresh(
            std::weak_ptr<PxConsoleClientImpl<ClientType>> weak_self) {
            while (true) {
                PanelRtcConfigRefreshAttempt attempt;
                std::shared_ptr<PanelRtcConfigRefreshGate> gate;
                std::shared_ptr<PxContext> context;
                asio::any_io_executor executor;
                std::string host;
                int port = 0;
                {
                    const auto current = weak_self.lock();
                    if (!current || current->stopping_ || !current->rtc_config_scope_) {
                        co_return;
                    }
                    gate = current->rtc_config_refresh_gate_;
                    attempt = gate->CurrentAttempt();
                    context = current->context_;
                    executor = current->rtc_config_scope_->Executor();
                    host = current->host_;
                    port = current->port_;
                }
                auto result = co_await FetchRtcConfig(
                    std::move(context), std::move(executor),
                    std::move(host), port, grApp->GetAppkey());
                const auto current = weak_self.lock();
                if (!current || current->stopping_) {
                    static_cast<void>(gate->FinishAttempt(attempt.sequence));
                    co_return;
                }
                if (!result.HasValue()) {
                    LOGW("Pull Panel RTC ICE configuration failed: code={}, stage={}, reason={}",
                         result.Error().StableCode(), result.Error().stage, result.Error().message);
                }
                else if (const auto response = result.TakeValue(); response.status != 200) {
                    LOGW("Pull Panel RTC ICE configuration failed, status={}", response.status);
                }
                else {
                    try {
                        const auto envelope = nlohmann::json::parse(response.body);
                        const auto data = envelope.at("data");
                        const auto revision = data.value("revision", 0ULL);
                        if (envelope.value("code", -1) == 200
                            && revision >= attempt.expected_revision
                            && revision > current->rtc_config_revision_.load()) {
                            {
                                std::scoped_lock lock(current->rtc_config_mutex_);
                                current->rtc_config_json_ = data.dump();
                            }
                            current->rtc_config_revision_ = revision;
                            LOGI("Panel RTC ICE configuration updated, revision={}", revision);
                            current->context_->SendAppMessage(MsgRtcIceConfigUpdated {
                                .revision_ = revision,
                            });
                        }
                    }
                    catch (const std::exception& error) {
                        LOGE("Parse Panel RTC ICE configuration failed: {}", error.what());
                    }
                }
                if (!gate->FinishAttempt(attempt.sequence)) {
                    co_return;
                }
            }
            co_return;
        }

        void HandleRecordListReq(const console_panel::RecordListReq& req) {
            LOGI("RecordListReq: {}", req.req_id());
            const auto attempt = record_list_request_gate_->TryStart();
            if (attempt.result == RecordListRequestStartResult::kLimitReached) {
                SendRecordListResponse(req.req_id(), {}, "too many record list requests");
                return;
            }
            if (attempt.result != RecordListRequestStartResult::kStarted) {
                SendRecordListResponse(req.req_id(), {}, "record list service is stopping");
                return;
            }
            const auto scope = record_list_scope_;
            if (!scope) {
                record_list_request_gate_->Finish(attempt.sequence);
                SendRecordListResponse(req.req_id(), {}, "record list worker is unavailable");
                return;
            }
            const auto weak_self = this->weak_from_this();
            const auto gate = record_list_request_gate_;
            const auto req_id = req.req_id();
            if (!scope->Spawn("panel-record-list", [weak_self, gate, attempt, req_id]() {
                    return RunRecordListRequest(weak_self, gate, attempt, req_id);
                })) {
                record_list_request_gate_->Finish(attempt.sequence);
                SendRecordListResponse(req_id, {}, "record list request was rejected");
            }
        }

        static PxAwaitable<PxResult<std::vector<RecordFileInfo>>> ScanRecordFilesAsync(
            std::shared_ptr<TaskRuntime> blocking_runtime,
            asio::any_io_executor executor,
            fs::path records_dir,
            std::shared_ptr<std::atomic_bool> cancellation_signal) {
            const auto operation = PxAsyncOneShot<std::vector<RecordFileInfo>>::Create(executor);
            const auto task_id = blocking_runtime->Post(SimpleThreadTask::Make(
                [operation, records_dir = std::move(records_dir), cancellation_signal]() {
                    try {
                        static_cast<void>(operation->TryComplete(
                            PxResult<std::vector<RecordFileInfo>>::Success(
                                ScanRecordFiles(records_dir, cancellation_signal))));
                    }
                    catch (const std::exception& error) {
                        static_cast<void>(operation->TryFail(MakePxAsyncError(
                            PxAsyncErrorCode::kProtocolError,
                            "panel_record_list_scan",
                            error.what(),
                            true)));
                    }
                }));
            if (task_id == 0) {
                static_cast<void>(operation->TryFail(MakePxAsyncError(
                    PxAsyncErrorCode::kCancelled,
                    "panel_record_list_scan",
                    "record list worker is stopping")));
            }
            co_return co_await PxAsyncOneShot<std::vector<RecordFileInfo>>::WaitUntil(
                operation, std::chrono::steady_clock::now() + std::chrono::seconds(30));
        }

        static PxAwaitable<void> RunRecordListRequest(
            std::weak_ptr<PxConsoleClientImpl<ClientType>> weak_self,
            std::shared_ptr<RecordListRequestGate> gate,
            RecordListRequestAttempt attempt,
            std::string req_id) {
            std::shared_ptr<TaskRuntime> blocking_runtime;
            asio::any_io_executor executor;
            {
                const auto current = weak_self.lock();
                if (!current || current->stopping_ || !current->record_list_scope_
                    || !current->record_list_blocking_runtime_) {
                    gate->Finish(attempt.sequence);
                    co_return;
                }
                blocking_runtime = current->record_list_blocking_runtime_;
                executor = current->record_list_scope_->Executor();
            }
            auto result = co_await ScanRecordFilesAsync(
                std::move(blocking_runtime), std::move(executor),
                fs::path(GetRenderRecordsDir()), attempt.cancellation_signal);
            attempt.cancellation_signal->store(true, std::memory_order_release);
            gate->Finish(attempt.sequence);

            const auto cancellation = co_await asio::this_coro::cancellation_state;
            if (cancellation.cancelled() != asio::cancellation_type::none) {
                co_return;
            }
            const auto current = weak_self.lock();
            if (!current || current->stopping_) {
                co_return;
            }
            if (!result.HasValue()) {
                current->SendRecordListResponse(
                    req_id, {}, std::format("{}: {}", result.Error().StableCode(),
                                            result.Error().message));
                co_return;
            }
            current->SendRecordListResponse(req_id, result.TakeValue(), "");
            co_return;
        }

        void SendRecordListResponse(
            const std::string& req_id,
            const std::vector<RecordFileInfo>& files,
            const std::string& error) {
            console_panel::ConsolePanelMessage msg;
            msg.set_msg_type(console_panel::ConsolePanelMessageType::kRecordListResp);
            auto& sub = *msg.mutable_record_list_resp();
            sub.set_device_id(device_id_);
            sub.set_req_id(req_id);
            sub.set_error(error);
            for (const auto& info : files) {
                auto& file = *sub.add_files();
                file.set_name(info.name);
                file.set_size(static_cast<int64_t>(info.size));
                file.set_mtime(info.mtime);
                file.set_monitor(info.monitor);
                file.set_codec(info.codec);
            }
            PostBinMessage(msg.SerializeAsString());
        }

        void HandleRecordFetchReq(const console_panel::RecordFetchReq& req) {
            LOGI("RecordFetchReq: {} -> {}", req.filename(), req.upload_url());
            if (req.filename().empty() || req.token().empty() || req.upload_url().empty()) {
                LOGE("RecordFetchReq invalid params, filename: {}", req.filename());
                return;
            }
            if (!IsValidRecordFileName(req.filename())) {
                LOGE("RecordFetchReq invalid file name: {}", req.filename());
                return;
            }
            if (!fetch_queue_) {
                LOGE("RecordFetchReq but fetch queue is not ready");
                return;
            }
            RecordFetchTask task;
            task.device_id = device_id_;
            task.req_id = req.req_id();
            task.filename = req.filename();
            task.token = req.token();
            task.upload_url = req.upload_url();
            if (!fetch_queue_->Push(task)) {
                LOGW("RecordFetchReq duplicated, ignored: {}", req.filename());
                return;
            }
            StartRecordFetchPump();
        }

        void StartRecordFetchPump() {
            const auto queue = fetch_queue_;
            if (!queue || !queue->TryStartPump()) {
                return;
            }
            const auto scope = record_fetch_scope_;
            if (!scope) {
                queue->AbortPump();
                LOGE("RecordFetchReq cannot start because async scope is unavailable");
                return;
            }
            const auto weak_self = this->weak_from_this();
            if (!scope->Spawn("panel-record-fetch", [weak_self]() {
                    return RunRecordFetchPump(weak_self);
                })) {
                queue->AbortPump();
                LOGW("Panel record fetch pump was rejected by the async scope");
            }
        }

        static PxAwaitable<PxResult<std::string>> UploadRecordFileAsync(
            std::shared_ptr<TaskRuntime> blocking_runtime,
            asio::any_io_executor executor,
            RecordFetchTask task,
            std::string appkey,
            std::shared_ptr<std::atomic_bool> cancellation_signal) {
            const auto operation = PxAsyncOneShot<std::string>::Create(executor);
            const auto task_id = blocking_runtime->Post(SimpleThreadTask::Make(
                [operation, task = std::move(task), appkey = std::move(appkey),
                 cancellation_signal]() {
                    try {
                        static_cast<void>(operation->TryComplete(
                            PxResult<std::string>::Success(UploadRecordFile(
                                task, appkey, cancellation_signal))));
                    }
                    catch (const std::exception& error) {
                        static_cast<void>(operation->TryFail(MakePxAsyncError(
                            PxAsyncErrorCode::kProtocolError,
                            "panel_record_upload",
                            error.what(),
                            true)));
                    }
                }));
            if (task_id == 0) {
                static_cast<void>(operation->TryFail(MakePxAsyncError(
                    PxAsyncErrorCode::kCancelled,
                    "panel_record_upload",
                    "record upload worker is stopping")));
            }
            co_return co_await PxAsyncOneShot<std::string>::WaitUntil(
                operation, std::chrono::steady_clock::now() + std::chrono::seconds(3605));
        }

        static PxAwaitable<bool> WaitForRecordFetchRetry(
            asio::any_io_executor executor,
            std::chrono::milliseconds delay) {
            auto timer = std::make_shared<asio::steady_timer>(executor);
            timer->expires_after(delay);
            asio::error_code wait_error;
            co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, wait_error));
            const auto cancellation = co_await asio::this_coro::cancellation_state;
            co_return !wait_error
                && cancellation.cancelled() == asio::cancellation_type::none;
        }

        static PxAwaitable<void> RunRecordFetchPump(
            std::weak_ptr<PxConsoleClientImpl<ClientType>> weak_self) {
            std::shared_ptr<RecordFetchQueue> queue;
            {
                const auto current = weak_self.lock();
                if (!current || current->stopping_ || !current->fetch_queue_) {
                    co_return;
                }
                queue = current->fetch_queue_;
            }
            LOGI("record fetch coroutine started");
            while (!queue->IsStopped()) {
                RecordFetchTask task;
                if (!queue->TryPop(task)) {
                    if (queue->KeepPumpRunning()) {
                        continue;
                    }
                    LOGI("record fetch coroutine idle");
                    co_return;
                }

                const auto cancellation_signal = std::make_shared<std::atomic_bool>(false);
                std::shared_ptr<TaskRuntime> blocking_runtime;
                asio::any_io_executor executor;
                {
                    const auto current = weak_self.lock();
                    if (!current || current->stopping_ || !current->record_fetch_scope_
                        || !current->record_fetch_blocking_runtime_) {
                        queue->Finish(task.filename);
                        queue->AbortPump();
                        co_return;
                    }
                    blocking_runtime = current->record_fetch_blocking_runtime_;
                    executor = current->record_fetch_scope_->Executor();
                    current->active_record_upload_cancellation_.store(cancellation_signal);
                }
                auto result = co_await UploadRecordFileAsync(
                    std::move(blocking_runtime), executor, task, grApp->GetAppkey(),
                    cancellation_signal);
                cancellation_signal->store(true, std::memory_order_release);
                if (const auto current = weak_self.lock()) {
                    current->active_record_upload_cancellation_.store({});
                }
                const auto cancellation = co_await asio::this_coro::cancellation_state;
                if (cancellation.cancelled() != asio::cancellation_type::none) {
                    queue->Finish(task.filename);
                    queue->AbortPump();
                    co_return;
                }

                std::string err;
                if (result.HasValue()) {
                    err = result.TakeValue();
                }
                else {
                    err = std::format("{}: {}", result.Error().StableCode(),
                                      result.Error().message);
                }
                if (err.empty()) {
                    queue->Finish(task.filename);
                    if (const auto current = weak_self.lock();
                        current && !current->stopping_) {
                        current->SendRecordFetchDone(task, true, "");
                    }
                    continue;
                }

                task.attempt += 1;
                LOGE("upload record failed (attempt {}/{}): {}, {}",
                     task.attempt, RecordFetchQueue::kMaxAttempts, task.filename, err);
                if (task.attempt >= RecordFetchQueue::kMaxAttempts) {
                    queue->Finish(task.filename);
                    if (const auto current = weak_self.lock();
                        current && !current->stopping_) {
                        current->SendRecordFetchDone(task, false, err);
                    }
                    continue;
                }

                const auto delay = RecordFetchQueue::RetryDelayMs(task.attempt);
                if (!co_await WaitForRecordFetchRetry(
                        executor, std::chrono::milliseconds(delay))) {
                    queue->Finish(task.filename);
                    queue->AbortPump();
                    co_return;
                }
                if (!queue->Requeue(task)) {
                    queue->Finish(task.filename);
                    queue->AbortPump();
                    co_return;
                }
            }
            queue->AbortPump();
            LOGI("record fetch coroutine stopped");
            co_return;
        }

        static std::string UploadRecordFile(
            const RecordFetchTask& task,
            const std::string& appkey,
            const std::shared_ptr<std::atomic_bool>& cancellation_signal) {
            const fs::path file_path = fs::path(GetRenderRecordsDir()) / task.filename;
            std::error_code ec;
            if (!fs::exists(file_path, ec) || !fs::is_regular_file(file_path, ec)) {
                return "file not found on device";
            }
            if (HasRecordingSidecar(file_path)) {
                return "file is still recording";
            }

            bool ssl = false;
            std::string host;
            std::string upath;
            int port = 0;
            if (!ParseUploadUrl(task.upload_url, ssl, host, port, upath)) {
                return std::format("invalid upload url: {}", task.upload_url);
            }

            const uint64_t file_size = fs::file_size(file_path, ec);
            if (ec) {
                return "stat file failed";
            }
            const int64_t mtime = FileMtimeSeconds(file_path);

            // 1GB segments over a 100Mbps link take minutes; allow 1 hour
            auto client = ssl ? HttpClient::MakeSSL(host, port, upath, 3600 * 1000)
                              : HttpClient::Make(host, port, upath, 3600 * 1000);
            client->SetCancellationSignal(cancellation_signal);
            auto resp = client->PostMultiPart(
                {
                    {"appkey", appkey},
                    {"token", task.token},
                    {"device_id", task.device_id},
                    {"filename", task.filename},
                    {"size", std::to_string(file_size)},
                    {"mtime", std::to_string(mtime)},
                },
                {},
                {{"file", file_path.string()}});
            if (resp.status != 200) {
                return std::format("http status: {}, err: {}", resp.status, resp.error_message);
            }
            try {
                const auto body = nlohmann::json::parse(resp.body);
                if (body.value("code", -1) != 200) {
                    return std::format("console rejected: {}", body.value("message", std::string("unknown")));
                }
            }
            catch (const std::exception& e) {
                return std::format("bad console response: {}", e.what());
            }
            LOGI("upload record ok: {}, {} bytes", task.filename, file_size);
            return "";
        }

        void SendRecordFetchDone(const RecordFetchTask& task, bool ok, const std::string& error) {
            console_panel::ConsolePanelMessage msg;
            msg.set_msg_type(console_panel::ConsolePanelMessageType::kRecordFetchDone);
            auto sub = msg.mutable_record_fetch_done();
            sub->set_device_id(task.device_id);
            sub->set_req_id(task.req_id);
            sub->set_filename(task.filename);
            sub->set_ok(ok);
            sub->set_error(error);
            PostBinMessage(msg.SerializeAsString());
        }

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<PxAsyncRuntime> async_runtime_{};
        std::shared_ptr<ClientType> client_ = nullptr;
        std::shared_ptr<PxAsyncScope> connection_scope_{};
        std::shared_ptr<PxReconnectSupervisor> reconnect_supervisor_{};
        std::string host_;
        int port_ = 0;
        std::string device_id_;
        std::string appkey_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::atomic_int64_t hb_idx_ = 0;
        std::atomic_bool use_legacy_cms_path_ = false;
        std::atomic_bool stopping_ = false;
        std::atomic_bool deferred_stop_scheduled_{false};
        std::atomic_uint64_t callback_generation_{0};
        mutable std::mutex network_mutex_{};
        std::mutex operation_mutex_{};
        std::shared_ptr<PxAsyncScope> rtc_config_scope_ = nullptr;
        std::shared_ptr<PanelRtcConfigRefreshGate> rtc_config_refresh_gate_ =
            PanelRtcConfigRefreshGate::Create();
        std::atomic_uint64_t rtc_config_revision_ = 0;
        std::mutex rtc_config_mutex_;
        std::string rtc_config_json_;
        int64_t last_received_timestamp_ = 0;
        Mutex<std::shared_ptr<SysInfo>> sys_info_;

        // serial record-upload queue (design doc 7.2)
        std::shared_ptr<RecordFetchQueue> fetch_queue_ = nullptr;
        std::shared_ptr<PxAsyncScope> record_fetch_scope_ = nullptr;
        std::shared_ptr<TaskRuntime> record_fetch_blocking_runtime_ = nullptr;
        std::atomic<std::shared_ptr<std::atomic_bool>> active_record_upload_cancellation_;
        std::shared_ptr<PxAsyncScope> record_list_scope_ = nullptr;
        std::shared_ptr<TaskRuntime> record_list_blocking_runtime_ = nullptr;
        std::shared_ptr<RecordListRequestGate> record_list_request_gate_ =
            RecordListRequestGate::Create();
    };

}

#endif //PX_PANEL_CONSOLE_CLIENT_IMPL_H
