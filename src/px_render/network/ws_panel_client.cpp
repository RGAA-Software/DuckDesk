//
// Created by RGAA on 2024-04-20.
//

#include "ws_panel_client.h"
#include <algorithm>
#include <px_common_new/string_util.h>
#include "rd_context.h"
#include "app/app_messages.h"
#include "rd_statistics.h"
#include "settings/rd_settings.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/async_mailbox.h"
#include "px_common_new/async_scope_drain.h"
#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/connection_attempt_workflow.h"
#include "px_common_new/reconnect_backoff.h"
#include "px_message.pb.h"
#include "px_render_panel_message.pb.h"
#include "px_render/modules/render_module_registry.h"
#include "px_render/plugin_interface/px_plugin_interface.h"
#include "px_render/plugin_interface/px_net_plugin.h"
#include "px_message_new/proto_converter.h"
#include "px_message_new/rp_proto_converter.h"
#include "px_common_new/time_util.h"
#include "architecture/runtime/render_composition_root.h"
#include "architecture/services/voice_call_service.h"
#include "architecture/sinks/media_recorder_sink.h"
#include <Windows.h>
#include <format>

namespace px
{

    constexpr int kMaxClientQueuedMessage = 1024;
    constexpr auto kPanelConnectionTimeout = std::chrono::seconds(10);
    constexpr std::size_t kIncomingPanelMessageCapacity = 1024;
    const PxReconnectBackoffOptions kPanelReconnectOptions{
        .initial_delay = std::chrono::milliseconds(250),
        .maximum_delay = std::chrono::seconds(30),
        .multiplier = 2.0,
        .jitter_ratio = 0.2,
    };

    WsPanelClient::WsPanelClient(const std::shared_ptr<RdContext>& ctx) : settings_(*RdSettings::Instance()) {
        statistics_ = RdStatistics::Instance();
        context_ = ctx;
        module_registry_ = context_->GetRenderModuleRegistry();
        composition_root_ = context_->GetRenderCompositionRoot();
        instance_id_ = std::format("{}-{}", GetCurrentProcessId(), TimeUtil::GetCurrentTimestamp());
    }

    WsPanelClient::~WsPanelClient() {
        Exit();
    }

    void WsPanelClient::Start() {
        if (started_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        // Console-scheduled app instances are supervised through px_service
        // and deliberately have no local Panel endpoint.  A zero port is the
        // explicit disabled value from RdSettings, not a connectable address.
        // Do this before creating listeners or reconnect state so shutdown
        // remains a no-op and no retry loop is left behind.
        if (settings_.get().panel_server_port_ <= 0) {
            LOGI("Render Panel client disabled: no panel server port configured");
            started_.store(false, std::memory_order_release);
            return;
        }
        exiting_.store(false, std::memory_order_release);
        const auto async_runtime = context_ ? context_->GetAsyncRuntime() : std::shared_ptr<PxAsyncRuntime>{};
        if (!async_runtime || async_runtime->IsStopping()) {
            LOGE("event=module.start component=render_panel code=ASYNC_RUNTIME_UNAVAILABLE "
                 "operation=start_client outcome=failed recoverable=false");
            Exit();
            return;
        }
        async_scope_ = PxAsyncScope::Create(async_runtime, PxAsyncLane::kState);
        msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kControl);
        state_msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kState);
        auto weak_self = weak_from_this();
        state_msg_listener_->Listen<MsgTimer500>([weak_self](const MsgTimer500&) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->ReportStatistics();
                    }
                });
            }
        });

        msg_listener_->Listen<MsgClientConnected>([weak_self](const MsgClientConnected&) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->ReportStatistics();
                    }
                });
            }
        });

        msg_listener_->Listen<MsgClientDisconnected>([weak_self](const MsgClientDisconnected&) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->context_->PostTask([weak_self]() {
                    if (auto self = weak_self.lock(); self && !self->exiting_) {
                        self->ReportStatistics();
                    }
                });
            }
        });

        client_ = std::make_shared<asio2::ws_client>();
        connection_workflow_ = PxConnectionAttemptWorkflow::Create(async_runtime, kPanelConnectionTimeout);
        connection_backoff_ = PxReconnectBackoff::Create(kPanelReconnectOptions);
        if (async_scope_) {
            incoming_messages_ = PxAsyncMailbox<std::string>::Create(async_scope_->Executor(), kIncomingPanelMessageCapacity);
        }
        if (!connection_workflow_ || !connection_backoff_ || !async_scope_ || !incoming_messages_) {
            LOGE("event=module.start component=render_panel code=ASYNC_WORKFLOW_CREATE_FAILED "
                 "operation=start_client outcome=failed recoverable=false");
            Exit();
            return;
        }
        if (!async_scope_->Spawn("render-panel-receive-loop", [weak_self, mailbox = incoming_messages_]() {
                return RunIncomingMessageLoop(weak_self, mailbox);
            })) {
            LOGE("event=module.start component=render_panel code=ASYNC_SCOPE_SPAWN_FAILED "
                 "operation=start_receive_loop outcome=failed recoverable=false");
            Exit();
            return;
        }
        client_->set_auto_reconnect(false);
        client_->set_timeout(std::chrono::milliseconds(2000));
        //client_->set_verify_mode(asio::ssl::verify_peer);
        client_->bind_init([weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_ && self->client_
                && self->connection_workflow_) {
                self->client_->ws_stream().binary(true);
                self->client_->set_no_delay(true);
                self->client_->ws_stream().set_option(
                    websocket::stream_base::decorator([](websocket::request_type &req) {
                        req.set(http::field::authorization, "websocket-client-authorization");}
                    )
                );
            }

        })
        .bind_connect([weak_self]() {
            if (asio2::get_last_error()) {
                auto wstr = StringUtil::ToWString(asio2::last_error_msg());
                auto str = StringUtil::ToUTF8(wstr);
                LOGE("event=transport.connection_attempt component=render_panel code={} operation=connect outcome=failure "
                     "recoverable=true reason={}", asio2::last_error_val(), str);
                if (auto self = weak_self.lock(); self && !self->exiting_
                    && self->connection_workflow_) {
                    const auto generation = self->connection_generation_.load(std::memory_order_acquire);
                    static_cast<void>(self->connection_workflow_->FailActive(
                        generation, MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "render-panel.connect", str, true)));
                }
            } else {
                if (auto self = weak_self.lock(); self && !self->exiting_ && self->client_) {
                    LOGI("WsPanelClient, connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
                }
            }
        })
        .bind_disconnect([weak_self]() {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                if (self->connection_workflow_) {
                    const auto generation = self->connection_generation_.load(std::memory_order_acquire);
                    static_cast<void>(self->connection_workflow_->MarkDisconnected(
                        generation,
                        MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "render-panel.disconnect", "Render disconnected from Panel", true)));
                }
            }
        })
        .bind_upgrade([weak_self]() {
            if (asio2::get_last_error()) {
                LOGE("event=transport.connection_attempt component=render_panel code={} operation=upgrade outcome=failure "
                     "recoverable=true reason={}", asio2::last_error_val(), asio2::last_error_msg());
                if (auto self = weak_self.lock(); self && !self->exiting_
                    && self->connection_workflow_) {
                    const auto generation = self->connection_generation_.load(std::memory_order_acquire);
                    static_cast<void>(self->connection_workflow_->FailActive(
                        generation,
                        MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "render-panel.upgrade", asio2::last_error_msg(), true)));
                }
                return;
            }
            if (auto self = weak_self.lock(); self && !self->exiting_
                && self->connection_workflow_) {
                static_cast<void>(
                    self->connection_workflow_->MarkReady(self->connection_generation_.load(std::memory_order_acquire)));
            }
        })
        .bind_recv([weak_self](std::string_view data) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                const auto mailbox = self->incoming_messages_;
                if (!mailbox) {
                    return;
                }
                const auto published = mailbox->TryPush(std::string(data));
                if (!published) {
                    LOGE("Render Panel receive mailbox rejected message: code={}, depth={}",
                         published.Error().StableCode(), mailbox->Statistics().depth);
                }
            }
        });

        LOGI("Will connect to panel : {}:{}", settings_.get().panel_server_host_, settings_.get().panel_server_port_);
        const auto panel_path = std::format("/panel/renderer?instance_id={}", instance_id_);
        if (!async_scope_->Spawn("render-panel-connection-loop",
                                 [weak_self, workflow = connection_workflow_, backoff = connection_backoff_, client = client_,
                                  host = settings_.get().panel_server_host_, port = settings_.get().panel_server_port_, panel_path]() mutable {
            return RunConnectionLoop(weak_self, workflow, backoff, client, std::move(host), port, std::move(panel_path));
        })) {
            LOGE("event=module.start component=render_panel code=ASYNC_SCOPE_SPAWN_FAILED "
                 "operation=start_connection_loop outcome=failed recoverable=false");
            Exit();
        }
    }

    PxAwaitable<void> WsPanelClient::RunIncomingMessageLoop(
        std::weak_ptr<WsPanelClient> weak_client,
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
            self->ParseNetMessage(message.Value());
        }
    }

    PxAwaitable<void> WsPanelClient::RunConnectionLoop(
        std::weak_ptr<WsPanelClient> weak_client,
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

            LOGI("event=transport.connection_attempt component=render_panel generation={}", ticket.generation);
            if (!client->async_start(host, port, path)) {
                static_cast<void>(workflow->FailActive(
                    ticket.generation,
                    MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "render-panel.start", asio2::last_error_msg(), true)));
            }

            const auto ready = co_await PxConnectionAttemptWorkflow::WaitUntilReady(
                workflow, ticket, std::chrono::steady_clock::now() + kPanelConnectionTimeout);
            if (ready) {
                backoff->Reset();
                LOGI("event=transport.connection_ready component=render_panel generation={}", ticket.generation);
                if (self = weak_client.lock(); self && !self->exiting_.load(std::memory_order_acquire)) {
                    self->ReportStatistics();
                }
                self.reset();
                const auto disconnected = co_await PxConnectionAttemptWorkflow::WaitUntilDisconnected(workflow, ticket);
                if (!disconnected) {
                    if (disconnected.Error().code == PxAsyncErrorCode::kServiceStopped ||
                        disconnected.Error().code == PxAsyncErrorCode::kCancelled) {
                        co_return;
                    }
                    LOGW("event=transport.connection_lost component=render_panel generation={} stage={} code={} recoverable={}",
                         ticket.generation, disconnected.Error().stage, disconnected.Error().StableCode(),
                         disconnected.Error().retryable);
                } else {
                    const auto& reason = disconnected.Value().reason;
                    LOGW("event=transport.connection_lost component=render_panel generation={} stage={} code={} recoverable={}",
                         ticket.generation, reason.stage, reason.StableCode(), reason.retryable);
                }
            } else if (ready.Error().code == PxAsyncErrorCode::kServiceStopped || ready.Error().code == PxAsyncErrorCode::kCancelled) {
                co_return;
            } else {
                LOGW("event=transport.connection_lost component=render_panel generation={} stage={} code={} recoverable={}",
                     ticket.generation, ready.Error().stage, ready.Error().StableCode(), ready.Error().retryable);
            }

            const auto step = backoff->Next();
            LOGI("event=transport.reconnect_wait component=render_panel generation={} attempt={} delay_ms={}",
                 ticket.generation, step.attempt, step.delay.count());
            const auto waited = co_await PxReconnectBackoff::Wait(step.delay);
            if (!waited) {
                co_return;
            }
        }
    }

    void WsPanelClient::Exit() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        const auto scope = BeginStop();
        if (scope && scope->IsScopeThread()) {
            LOGI("event=async.scope_drain component=render_panel operation=stop_client outcome=deferred "
                 "reason=shutdown_requested_from_runtime_thread outstanding={}",
                 scope->GetStatistics().outstanding);
            return;
        }
        const auto client = client_;
        const auto remaining = std::max(
            std::chrono::milliseconds::zero(),
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
        const auto scope_drained = !scope || scope->WaitFor(remaining);
        static_cast<void>(RequestAsioClientStop(client, "render-panel.adapter-stop-confirm"));
        const auto adapter_stopped = WaitForAsioClientStoppedBlocking(client, deadline);
        if (!scope_drained || !adapter_stopped) {
            LOGE("event=async.scope_drain component=render_panel code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                 "operation=stop_client outcome=timeout recoverable=false outstanding={}",
                 scope ? scope->GetStatistics().outstanding : 0);
            return;
        }
        FinishStop();
    }

    PxAwaitable<PxResult<void>> WsPanelClient::StopAsync(
        const std::shared_ptr<WsPanelClient>& owner,
        const std::chrono::steady_clock::time_point deadline) {
        if (!owner) {
            co_return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kInvalidArgument, "render-panel.stop", "Render Panel client owner is missing"));
        }
        const auto scope = owner->BeginStop();
        if (scope) {
            const auto drained = co_await WaitForAsyncScopeDrain(scope, deadline, "render-panel.stop");
            if (!drained) {
                LOGE("event=async.scope_drain component=render_panel code={} operation=stop_client "
                     "outcome=failed recoverable={} outstanding={} reason={}",
                     drained.Error().StableCode(),
                     drained.Error().retryable,
                     scope->GetStatistics().outstanding,
                     drained.Error().message);
                co_return PxResult<void>::Failure(drained.Error());
            }
        }
        static_cast<void>(RequestAsioClientStop(owner->client_, "render-panel.adapter-stop-confirm"));
        const auto adapter_stopped = co_await WaitForAsioClientStopped(
            owner->client_, deadline, "render-panel.adapter-stop");
        if (!adapter_stopped) {
            co_return adapter_stopped;
        }
        owner->FinishStop();
        co_return PxResult<void>::Success();
    }

    std::shared_ptr<PxAsyncScope> WsPanelClient::BeginStop() {
        if (exiting_.exchange(true)) {
            return async_scope_;
        }
        if (msg_listener_) {
            msg_listener_->UnListenAll();
            msg_listener_.reset();
        }
        if (state_msg_listener_) {
            state_msg_listener_->UnListenAll();
            state_msg_listener_.reset();
        }
        if (incoming_messages_) {
            static_cast<void>(incoming_messages_->Close(
                MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "render-panel.receive", "Render Panel client is stopping")));
        }
        if (connection_workflow_) {
            connection_workflow_->Stop();
        }
        const auto client = client_;
        static_cast<void>(RequestAsioClientStop(client, "render-panel.adapter-stop"));
        if (async_scope_) {
            async_scope_->BeginStop();
        }
        return async_scope_;
    }

    void WsPanelClient::FinishStop() {
        if (async_scope_ && async_scope_->GetStatistics().outstanding != 0) {
            return;
        }
        client_.reset();
        incoming_messages_.reset();
        connection_workflow_.reset();
        connection_backoff_.reset();
        async_scope_.reset();
        started_.store(false, std::memory_order_release);
    }

    bool WsPanelClient::Alive() const {
        return client_ && client_->is_started()
            && connection_workflow_ && connection_workflow_->IsReady();
    }

    void WsPanelClient::ReportStatistics() {
        this->SendStatisticsInternal();
        this->SendPluginsInfoInternal();
    }

    void WsPanelClient::SendStatisticsInternal() {
        PostNetMessage(statistics_->AsProtoMessage());
    }

    void WsPanelClient::SendPluginsInfoInternal() {
        pxrp::RpMessage msg;
        msg.set_type(pxrp::kRpPluginsInfo);
        auto m_info = msg.mutable_plugins_info();
        auto plugins_info = m_info->mutable_plugins_info();
        for (const auto& module : module_registry_->SnapshotModuleInfo()) {
            auto info = plugins_info->Add();
            info->set_id(module.id);
            info->set_name(module.name);
            info->set_author(module.author);
            info->set_desc(module.description);
            info->set_version_name(module.version_name);
            info->set_version_code(static_cast<int32_t>(module.version_code));
            info->set_enabled(module.enabled);
        }
        if (composition_root_) {
            for (const auto& module : composition_root_->SnapshotModules()) {
                pxrp::RpPluginInfo info;
                info.set_id(module.descriptor.id);
                info.set_name(module.descriptor.name);
                info.set_author(module.descriptor.author);
                info.set_desc(module.descriptor.description);
                info.set_version_name(module.descriptor.version_name);
                info.set_version_code(
                    static_cast<int32_t>(module.descriptor.version_code));
                info.set_enabled(module.enabled);
                // NOLINTNEXTLINE(gammaray-raw-pointer-boundary): protobuf owns
                // the repeated-message element returned by this transient API.
                *plugins_info->Add() = std::move(info);
            }
        }
        auto buffer = RpProtoAsData(&msg);
        PostNetMessage(buffer);
    }

    void WsPanelClient::ReportMonitorChanged() {
        pxrp::RpMessage msg;
        msg.set_type(pxrp::kRpMonitorChanged);
        const auto sub = msg.mutable_monitor_changed();
        const auto buffer = RpProtoAsData(&msg);
        PostNetMessage(buffer);
    }

    bool WsPanelClient::PostNetMessage(std::shared_ptr<Data> msg) {
        if (!msg || exiting_) {
            return false;
        }
        auto client = client_;
        if (client && client->is_started() && connection_workflow_
            && connection_workflow_->IsReady()) {
            if (queuing_message_count_ >= kMaxClientQueuedMessage) {
                return false;
            }
            ++queuing_message_count_;
            auto weak_self = weak_from_this();
            client->async_send(msg->CStr(), msg->Size(), [weak_self]() {
                if (auto self = weak_self.lock(); self && !self->exiting_) {
                    --self->queuing_message_count_;
                }
            });
            return true;
        }
        return false;
    }

    void WsPanelClient::ParseNetMessage(const std::string& msg) {
        try {
            pxrp::RpMessage m;
            m.ParseFromString(msg);
            if (m.type() == pxrp::RpMessageType::kSyncPanelInfo) {
                const auto& sub = m.sync_panel_info();
                auto& settings = settings_.get();
                settings.device_id_ = sub.device_id();
                settings.device_random_pwd_ = sub.device_random_pwd();
                settings.device_safety_pwd_ = sub.device_safety_pwd();
                settings.relay_host_ = sub.relay_host();
                settings.relay_port_ = sub.relay_port();
                settings.can_be_operated_ = sub.can_be_operated();
                settings.relay_enabled_ = sub.relay_enabled();
                settings.language_ = sub.language();
                settings.file_transfer_enabled_ = sub.file_transfer_enabled();
                settings.audio_enabled_ = sub.audio_enabled();
                settings.appkey_ = sub.appkey();
                settings.max_transmit_speed_ = sub.max_transmit_speed();
                settings.max_receive_speed_ = sub.max_receive_speed();
                settings.role_ = sub.role();

                module_registry_->SyncModuleSettings(PxPluginSettingsInfo {
                    .device_id_ = settings.device_id_,
                    .device_random_pwd_ = settings.device_random_pwd_,
                    .device_safety_pwd_ = settings.device_safety_pwd_,
                    .relay_host_ = settings.relay_host_,
                    .relay_port_ = settings.relay_port_,
                    .can_be_operated_ = settings.can_be_operated_,
                    .direct_allow_takeover_ = settings.direct_allow_takeover_,
                    .relay_enabled_ = settings.relay_enabled_,
                    .language_ = settings.language_,
                    .file_transfer_enabled_ = settings.file_transfer_enabled_,
                    .audio_enabled_ = settings.audio_enabled_,
                    .appkey_ = settings.appkey_,
                    .max_transmit_speed_ = settings.max_transmit_speed_,
                    .max_receive_speed_ = settings.max_receive_speed_,
                    .role_ = settings.role_,
                });
            }
            else if (m.type() == pxrp::RpMessageType::kRpCommandRenderer) {
                LOGI("====> CommandRenderer <====");
                const auto& sub = m.command_renderer();
                int ws_port = sub.ws_port();
                const auto& plugin_id = sub.plugin_id();
                LOGI("Plugin id: {}", plugin_id);
                if (sub.command() == pxrp::RpPanelCommand::kEnablePlugin) {
                    ProcessCommandEnablePlugin(plugin_id);
                }
                else if (sub.command() == pxrp::RpPanelCommand::kDisablePlugin) {
                    ProcessCommandDisablePlugin(plugin_id);
                }
                else if (sub.command() == pxrp::RpPanelCommand::kStartMediaRecordServerSide ||
                         sub.command() == pxrp::RpPanelCommand::kStopMediaRecordServerSide) {
                    const auto recorder = context_->GetMediaRecorderSink();
                    if (!recorder || plugin_id != render::kMediaRecorderModuleId) {
                        LOGE("event=record.command component=ws_panel_client "
                             "module={} outcome=rejected reason=module_unavailable",
                             plugin_id);
                    }
                    else if (sub.command() ==
                             pxrp::RpPanelCommand::kStartMediaRecordServerSide) {
                        recorder->StartRecording();
                    }
                    else {
                        recorder->StopRecording();
                    }
                }
            }
            // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
#if 0
            else if (m.type() == pxrp::RpMessageType::kRpClipboardEvent) {
                const auto& clipboard_info = m.clipboard_info();
                // Clipboard payload logging is intentionally disabled because the content is sensitive.

                auto event = std::make_shared<MsgClipboardEvent>();
                event->clipboard_type_ = [&]() {
                   if (clipboard_info.type() == pxrp::RpClipboardType::kRpClipboardText) {
                       return MsgClipboardType::kText;
                   }
                   else {
                       return MsgClipboardType::kFiles;
                   }
                }();
                event->text_msg_ = clipboard_info.msg();
                for (const auto& file : clipboard_info.files()) {
                    event->files_.push_back(MsgClipboardFile {
                        .file_name_ = file.file_name(),
                        .full_path_ = file.full_path(),
                        .total_size_ = file.total_size(),
                        .ref_path_ = file.ref_path(),
                    });
                }
                if (const auto service = context_->GetVoiceCallService()) {
                    service->HandleConsentDecision(*event);
                }
            }
#endif
            else if (m.type() == pxrp::RpMessageType::kRpDisconnectConnection) {
                const auto& sub = m.disconnect_connection();
                // 1. make disconnect message in px_messages.proto
                auto resp_msg = std::make_shared<px::Message>();
                resp_msg->set_device_id(sub.device_id());
                resp_msg->set_stream_id(sub.stream_id());
                resp_msg->set_type(kDisconnectConnection);
                auto resp_sub = resp_msg->mutable_disconnect_connection();
                resp_sub->set_device_id(sub.device_id());
                resp_sub->set_stream_id(sub.stream_id());
                resp_sub->set_room_id(sub.room_id());
                resp_sub->set_device_name(sub.device_name());
                auto buffer = ProtoAsData(resp_msg);

                module_registry_->BroadcastTargetStreamMessage(
                    sub.stream_id(), buffer, true);
            }
            else if (m.type() == pxrp::RpMessageType::kRpVoiceCallConsentDecision) {
                const auto& sub = m.voice_call_consent_decision();
                auto event = std::make_shared<MsgVoiceCallConsentDecision>();
                event->stream_id_ = sub.stream_id();
                event->call_id_ = sub.call_id();
                event->request_id_ = sub.request_id();
                event->accepted_ = sub.accepted();
                event->reason_ = sub.reason();
                context_->DispatchAppEventToModules(event);
            }
            else if (m.type() == pxrp::RpMessageType::kRpRawRenderMessage) {
                const auto& sub = m.raw_render_msg();
                auto data = Data::From(sub.msg());
                LOGI("==> RawRenderMessage--> stream id: {}, data ch: {}", sub.stream_id(), sub.data_channel());
                if (sub.data_channel()) {
                    module_registry_->BroadcastFileTransferMessage(
                        sub.stream_id(), data, sub.run_through());
                }
                else {
                    module_registry_->BroadcastNetworkMessage(
                        data, sub.run_through());
                }
            }
            else if (m.type() == pxrp::RpMessageType::kRpHardwareInfo) {
                auto json_msg = m.hw_info().json_msg();
                px::Message net_msg;
                net_msg.set_type(MessageType::kHardwareInfo);
                net_msg.mutable_hw_info()->set_hw_info(json_msg);
                net_msg.mutable_hw_info()->set_current_cpu_freq(m.hw_info().current_cpu_freq());
                auto data = ProtoAsData(&net_msg);
                module_registry_->BroadcastNetworkMessage(data, true);
            }

        } catch(std::exception& e) {
            LOGE("ParseNetMessage failed: {}", e.what());
        }
    }

    void WsPanelClient::ProcessCommandEnablePlugin(const std::string& plugin_id) {
        if (composition_root_) {
            for (const auto& module : composition_root_->SnapshotModules()) {
                if (module.descriptor.id == plugin_id) {
                    static_cast<void>(
                        composition_root_->SetEnabled(plugin_id, true));
                    return;
                }
            }
        }
        static_cast<void>(module_registry_->SetModuleEnabled(plugin_id, true));
    }

    void WsPanelClient::ProcessCommandDisablePlugin(const std::string& plugin_id) {
        if (composition_root_) {
            for (const auto& module : composition_root_->SnapshotModules()) {
                if (module.descriptor.id == plugin_id) {
                    static_cast<void>(
                        composition_root_->SetEnabled(plugin_id, false));
                    return;
                }
            }
        }
        static_cast<void>(module_registry_->SetModuleEnabled(plugin_id, false));
    }

}
