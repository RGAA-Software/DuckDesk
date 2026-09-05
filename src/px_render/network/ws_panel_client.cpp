//
// Created by RGAA on 2024-04-20.
//

#include "ws_panel_client.h"
#include <algorithm>
#include <px_common/string_util.h>
#include "rd_context.h"
#include "app/app_messages.h"
#include "rd_statistics.h"
#include "settings/rd_settings.h"
#include "px_common/log.h"
#include "px_common/data.h"
#include "px_common/message_notifier.h"
#include "px_common/async_mailbox.h"
#include "px_common/async_scope_drain.h"
#include "px_common/asio_client_shutdown.h"
#include "px_common/reconnect_supervisor.h"
#include "px_common/websocket_reconnect_adapter.h"
#include "px_message.pb.h"
#include "px_render_panel_message.pb.h"
#include "px_render/modules/render_module_registry.h"
#include "px_message/proto_converter.h"
#include "px_message/rp_proto_converter.h"
#include "px_common/time_util.h"
#include "architecture/runtime/render_composition_root.h"
#include "architecture/services/voice_call_service.h"
#include "architecture/sinks/media_recorder_sink.h"
#include <Windows.h>
#include <format>

namespace px {

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
    adapter_slot_ = std::make_shared<PxReconnectAdapterSlot<asio2::ws_client>>();
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
    std::unique_lock operation_lock(operation_mutex_);
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
    deferred_exit_scheduled_.store(false, std::memory_order_release);
    const auto async_runtime = context_ ? context_->GetAsyncRuntime() : std::shared_ptr<PxAsyncRuntime>{};
    if (!async_runtime || async_runtime->IsStopping()) {
        LOGE("event=module.start component=render_panel code=ASYNC_RUNTIME_UNAVAILABLE "
             "operation=start_client outcome=failed recoverable=false");
        operation_lock.unlock();
        Exit();
        return;
    }
    const auto async_scope = PxAsyncScope::Create(async_runtime, PxAsyncLane::kState);
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

    const auto connection_supervisor = PxReconnectSupervisor::Create(async_runtime, PxReconnectSupervisorOptions{
                                                                                        .component = "render_panel",
                                                                                        .connection_timeout = kPanelConnectionTimeout,
                                                                                        .adapter_stop_timeout = std::chrono::seconds(3),
                                                                                        .backoff = kPanelReconnectOptions,
                                                                                    });
    const auto incoming_messages = async_scope ? PxAsyncMailbox<std::string>::Create(async_scope->Executor(), kIncomingPanelMessageCapacity)
                                               : std::shared_ptr<PxAsyncMailbox<std::string>>{};
    if (!connection_supervisor || !async_scope || !incoming_messages) {
        LOGE("event=module.start component=render_panel code=ASYNC_WORKFLOW_CREATE_FAILED "
             "operation=start_client outcome=failed recoverable=false");
        operation_lock.unlock();
        Exit();
        return;
    }
    {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        async_scope_ = async_scope;
        connection_supervisor_ = connection_supervisor;
        incoming_messages_ = incoming_messages;
    }
    if (!async_scope->Spawn("render-panel-receive-loop",
                            [weak_self, mailbox = incoming_messages]() { return RunIncomingMessageLoop(weak_self, mailbox); })) {
        LOGE("event=module.start component=render_panel code=ASYNC_SCOPE_SPAWN_FAILED "
             "operation=start_receive_loop outcome=failed recoverable=false");
        operation_lock.unlock();
        Exit();
        return;
    }
    LOGI("Will connect to panel : {}:{}", settings_.get().panel_server_host_, settings_.get().panel_server_port_);
    const auto panel_path = std::format("/panel/renderer?instance_id={}", instance_id_);
    const auto adapter_slot = adapter_slot_;
    const auto supervisor = connection_supervisor;
    const auto mailbox = incoming_messages;
    PxReconnectSupervisorHooks reconnect_hooks{
        .start_attempt =
            [weak_self, adapter_slot, supervisor, mailbox, host = settings_.get().panel_server_host_, port = settings_.get().panel_server_port_,
             panel_path](const std::uint64_t generation) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_.load(std::memory_order_acquire)) {
                    return PxResult<void>::Failure(
                        MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "render-panel.start", "Render Panel client is stopping"));
                }
                const auto client = std::make_shared<asio2::ws_client>();
                const auto weak_client = std::weak_ptr<asio2::ws_client>(client);
                client->set_auto_reconnect(false);
                client->set_timeout(std::chrono::milliseconds(2000));
                client
                    ->bind_init([weak_self, weak_client]() {
                        const auto owner = weak_self.lock();
                        const auto current = weak_client.lock();
                        if (!owner || !current || owner->exiting_.load(std::memory_order_acquire)) {
                            return;
                        }
                        current->ws_stream().binary(true);
                        current->set_no_delay(true);
                        current->ws_stream().set_option(websocket::stream_base::decorator(
                            [](websocket::request_type& request) { request.set(http::field::authorization, "websocket-client-authorization"); }));
                    })
                    .bind_connect([weak_self, weak_client, supervisor, generation]() {
                        const auto owner = weak_self.lock();
                        const auto current = weak_client.lock();
                        if (!owner || !current || owner->exiting_.load(std::memory_order_acquire)) {
                            return;
                        }
                        if (asio2::get_last_error()) {
                            const auto reason = StringUtil::ToUTF8(StringUtil::ToWString(asio2::last_error_msg()));
                            static_cast<void>(supervisor->FailActive(
                                generation, MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "render-panel.connect", reason, true)));
                            return;
                        }
                        LOGI("WsPanelClient, connect success : {} {} ", current->local_address().c_str(), current->local_port());
                    })
                    .bind_disconnect([weak_self, supervisor, generation]() {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire)) {
                            static_cast<void>(supervisor->MarkDisconnected(generation, MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected,
                                                                                                        "render-panel.disconnect",
                                                                                                        "Render disconnected from Panel", true)));
                        }
                    })
                    .bind_upgrade([weak_self, supervisor, generation]() {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire)) {
                            if (asio2::get_last_error()) {
                                static_cast<void>(
                                    supervisor->FailActive(generation, MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "render-panel.upgrade",
                                                                                        asio2::last_error_msg(), true)));
                                return;
                            }
                            static_cast<void>(supervisor->MarkReady(generation));
                        }
                    })
                    .bind_recv([weak_self, mailbox](std::string_view data) {
                        if (const auto owner = weak_self.lock(); owner && !owner->exiting_.load(std::memory_order_acquire)) {
                            const auto published = mailbox->TryPush(std::string(data));
                            if (!published) {
                                LOGE("Render Panel receive mailbox rejected message: code={}, depth={}", published.Error().StableCode(),
                                     mailbox->Statistics().depth);
                            }
                        }
                    });
                adapter_slot->Replace(client);
                return StartWebSocketAdapter(client, host, port, panel_path, "render-panel.start");
            },
        .stop_attempt =
            [adapter_slot](const std::chrono::steady_clock::time_point deadline) {
                return StopWebSocketAdapter(adapter_slot->Snapshot(), deadline, "render-panel.retry-reset");
            },
        .on_ready =
            [weak_self](std::uint64_t) {
                if (const auto self = weak_self.lock(); self && !self->exiting_.load(std::memory_order_acquire)) {
                    self->ReportStatistics();
                }
            },
    };
    if (!async_scope->Spawn("render-panel-connection-loop", [supervisor, hooks = std::move(reconnect_hooks)]() mutable {
            return PxReconnectSupervisor::Run(std::move(supervisor), std::move(hooks));
        })) {
        LOGE("event=module.start component=render_panel code=ASYNC_SCOPE_SPAWN_FAILED "
             "operation=start_connection_loop outcome=failed recoverable=false");
        operation_lock.unlock();
        Exit();
    }
}

PxAwaitable<void> WsPanelClient::RunIncomingMessageLoop(std::weak_ptr<WsPanelClient> weak_client,
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

void WsPanelClient::Exit() {
    std::unique_lock operation_lock(operation_mutex_);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto scope = BeginStop();
    if (scope && scope->IsScopeThread()) {
        LOGI("event=async.scope_drain component=render_panel operation=stop_client outcome=deferred "
             "reason=shutdown_requested_from_runtime_thread outstanding={}",
             scope->GetStatistics().outstanding);
        ScheduleDeferredExit();
        return;
    }
    const auto client = adapter_slot_ ? adapter_slot_->Snapshot() : std::shared_ptr<asio2::ws_client>{};
    const auto remaining = std::max(std::chrono::milliseconds::zero(),
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

void WsPanelClient::ScheduleDeferredExit() {
    if (deferred_exit_scheduled_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const auto runtime = context_ ? context_->GetAsyncRuntime() : std::shared_ptr<PxAsyncRuntime>{};
    const auto weak_self = weak_from_this();
    if (!runtime || !runtime->DeferBlocking([weak_self]() {
            if (const auto self = weak_self.lock()) {
                self->Exit();
            }
        })) {
        deferred_exit_scheduled_.store(false, std::memory_order_release);
        LOGE("event=async.scope_drain component=render_panel code=ASYNC_DEFER_FAILED operation=stop_client outcome=failed "
             "recoverable=false");
    }
}

PxAwaitable<PxResult<void>> WsPanelClient::StopAsync(std::shared_ptr<WsPanelClient> owner, const std::chrono::steady_clock::time_point deadline) {
    if (!owner) {
        co_return PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "render-panel.stop", "Render Panel client owner is missing"));
    }
    std::shared_ptr<PxAsyncScope> scope;
    {
        std::unique_lock operation_lock(owner->operation_mutex_);
        scope = owner->BeginStop();
    }
    if (scope) {
        const auto drained = co_await WaitForAsyncScopeDrain(scope, deadline, "render-panel.stop");
        if (!drained) {
            LOGE("event=async.scope_drain component=render_panel code={} operation=stop_client "
                 "outcome=failed recoverable={} outstanding={} reason={}",
                 drained.Error().StableCode(), drained.Error().retryable, scope->GetStatistics().outstanding, drained.Error().message);
            co_return PxResult<void>::Failure(drained.Error());
        }
    }
    const auto client = owner->adapter_slot_ ? owner->adapter_slot_->Snapshot() : std::shared_ptr<asio2::ws_client>{};
    static_cast<void>(RequestAsioClientStop(client, "render-panel.adapter-stop-confirm"));
    const auto adapter_stopped = co_await WaitForAsioClientStopped(client, deadline, "render-panel.adapter-stop");
    if (!adapter_stopped) {
        co_return adapter_stopped;
    }
    {
        std::unique_lock operation_lock(owner->operation_mutex_);
        owner->FinishStop();
    }
    co_return PxResult<void>::Success();
}

std::shared_ptr<PxAsyncScope> WsPanelClient::BeginStop() {
    const auto state = SnapshotAsyncState();
    if (exiting_.exchange(true)) {
        return state.scope;
    }
    if (msg_listener_) {
        msg_listener_->UnListenAll();
        msg_listener_.reset();
    }
    if (state_msg_listener_) {
        state_msg_listener_->UnListenAll();
        state_msg_listener_.reset();
    }
    if (state.mailbox) {
        static_cast<void>(
            state.mailbox->Close(MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "render-panel.receive", "Render Panel client is stopping")));
    }
    if (state.supervisor) {
        state.supervisor->Stop();
    }
    const auto client = adapter_slot_ ? adapter_slot_->Snapshot() : std::shared_ptr<asio2::ws_client>{};
    static_cast<void>(RequestAsioClientStop(client, "render-panel.adapter-stop"));
    if (state.scope) {
        state.scope->BeginStop();
    }
    return state.scope;
}

void WsPanelClient::FinishStop() {
    const auto state = SnapshotAsyncState();
    if (state.scope && state.scope->GetStatistics().outstanding != 0) {
        return;
    }
    if (adapter_slot_) {
        adapter_slot_->Clear();
    }
    {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        incoming_messages_.reset();
        connection_supervisor_.reset();
        async_scope_.reset();
    }
    queuing_message_count_.store(0, std::memory_order_release);
    started_.store(false, std::memory_order_release);
    deferred_exit_scheduled_.store(false, std::memory_order_release);
}

bool WsPanelClient::Alive() const {
    const auto state = SnapshotAsyncState();
    const auto client = adapter_slot_ ? adapter_slot_->Snapshot() : std::shared_ptr<asio2::ws_client>{};
    return client && client->is_started() && state.supervisor && state.supervisor->IsReady();
}

WsPanelClient::AsyncStateSnapshot WsPanelClient::SnapshotAsyncState() const {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    return AsyncStateSnapshot{
        .scope = async_scope_,
        .supervisor = connection_supervisor_,
        .mailbox = incoming_messages_,
    };
}

void WsPanelClient::ReportStatistics() {
    this->SendStatisticsInternal();
    this->SendModulesInfoInternal();
}

void WsPanelClient::SendStatisticsInternal() {
    PostNetMessage(statistics_->AsProtoMessage());
}

void WsPanelClient::SendModulesInfoInternal() {
    pxrp::RpMessage msg;
    msg.set_type(pxrp::kRpPluginsInfo);
    auto& m_info = *msg.mutable_plugins_info();
    auto& plugins_info = *m_info.mutable_plugins_info();
    for (const auto& module : module_registry_->SnapshotModuleInfo()) {
        auto& info = *plugins_info.Add();
        info.set_id(module.id);
        info.set_name(module.name);
        info.set_author(module.author);
        info.set_desc(module.description);
        info.set_version_name(module.version_name);
        info.set_version_code(static_cast<int32_t>(module.version_code));
        info.set_enabled(module.enabled);
    }
    if (composition_root_) {
        for (const auto& module : composition_root_->SnapshotModules()) {
            pxrp::RpPluginInfo info;
            info.set_id(module.descriptor.id);
            info.set_name(module.descriptor.name);
            info.set_author(module.descriptor.author);
            info.set_desc(module.descriptor.description);
            info.set_version_name(module.descriptor.version_name);
            info.set_version_code(static_cast<int32_t>(module.descriptor.version_code));
            info.set_enabled(module.enabled);
            // NOLINTNEXTLINE(gammaray-raw-pointer-boundary): protobuf owns
            // the repeated-message element returned by this transient API.
            *plugins_info.Add() = std::move(info);
        }
    }
    auto buffer = RpProtoAsData(&msg);
    PostNetMessage(buffer);
}

void WsPanelClient::ReportMonitorChanged() {
    pxrp::RpMessage msg;
    msg.set_type(pxrp::kRpMonitorChanged);
    const auto buffer = RpProtoAsData(&msg);
    PostNetMessage(buffer);
}

bool WsPanelClient::PostNetMessage(std::shared_ptr<Data> msg) {
    if (!msg || exiting_) {
        return false;
    }
    const auto state = SnapshotAsyncState();
    const auto client = adapter_slot_ ? adapter_slot_->Snapshot() : std::shared_ptr<asio2::ws_client>{};
    if (client && client->is_started() && state.supervisor && state.supervisor->IsReady()) {
        if (queuing_message_count_ >= kMaxClientQueuedMessage) {
            return false;
        }
        ++queuing_message_count_;
        auto weak_self = weak_from_this();
        client->async_send(msg->Bytes().data(), msg->Size(), [weak_self]() {
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

            module_registry_->SyncModuleSettings(RenderRuntimeSettings{
                .device_id = settings.device_id_,
                .device_random_password = settings.device_random_pwd_,
                .device_safety_password = settings.device_safety_pwd_,
                .relay_host = settings.relay_host_,
                .relay_port = settings.relay_port_,
                .can_be_operated = settings.can_be_operated_,
                .direct_allow_takeover = settings.direct_allow_takeover_,
                .relay_enabled = settings.relay_enabled_,
                .language = settings.language_,
                .file_transfer_enabled = settings.file_transfer_enabled_,
                .audio_enabled = settings.audio_enabled_,
                .appkey = settings.appkey_,
                .max_transmit_speed = settings.max_transmit_speed_,
                .max_receive_speed = settings.max_receive_speed_,
                .role = settings.role_,
            });
        } else if (m.type() == pxrp::RpMessageType::kRpCommandRenderer) {
            LOGI("====> CommandRenderer <====");
            const auto& sub = m.command_renderer();
            int ws_port = sub.ws_port();
            const auto& module_id = sub.plugin_id();
            LOGI("Module id: {}", module_id);
            if (sub.command() == pxrp::RpPanelCommand::kEnablePlugin) {
                ProcessCommandEnableModule(module_id);
            } else if (sub.command() == pxrp::RpPanelCommand::kDisablePlugin) {
                ProcessCommandDisableModule(module_id);
            } else if (sub.command() == pxrp::RpPanelCommand::kStartMediaRecordServerSide ||
                       sub.command() == pxrp::RpPanelCommand::kStopMediaRecordServerSide) {
                const auto recorder = context_->GetMediaRecorderSink();
                if (!recorder || module_id != render::kMediaRecorderModuleId) {
                    LOGE("event=record.command component=ws_panel_client "
                         "module={} outcome=rejected reason=module_unavailable",
                         module_id);
                } else if (sub.command() == pxrp::RpPanelCommand::kStartMediaRecordServerSide) {
                    recorder->StartRecording();
                } else {
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
            auto& resp_sub = *resp_msg->mutable_disconnect_connection();
            resp_sub.set_device_id(sub.device_id());
            resp_sub.set_stream_id(sub.stream_id());
            resp_sub.set_room_id(sub.room_id());
            resp_sub.set_device_name(sub.device_name());
            auto buffer = ProtoAsData(resp_msg);

            module_registry_->BroadcastTargetStreamMessage(sub.stream_id(), buffer, true);
        } else if (m.type() == pxrp::RpMessageType::kRpVoiceCallConsentDecision) {
            const auto& sub = m.voice_call_consent_decision();
            auto event = std::make_shared<MsgVoiceCallConsentDecision>();
            event->stream_id_ = sub.stream_id();
            event->call_id_ = sub.call_id();
            event->request_id_ = sub.request_id();
            event->accepted_ = sub.accepted();
            event->reason_ = sub.reason();
            context_->DispatchAppEventToModules(event);
        } else if (m.type() == pxrp::RpMessageType::kRpRawRenderMessage) {
            const auto& sub = m.raw_render_msg();
            auto data = Data::From(sub.msg());
            LOGI("==> RawRenderMessage--> stream id: {}, data ch: {}", sub.stream_id(), sub.data_channel());
            if (sub.data_channel()) {
                module_registry_->BroadcastFileTransferMessage(sub.stream_id(), data, sub.run_through());
            } else {
                module_registry_->BroadcastNetworkMessage(data, sub.run_through());
            }
        } else if (m.type() == pxrp::RpMessageType::kRpHardwareInfo) {
            auto json_msg = m.hw_info().json_msg();
            px::Message net_msg;
            net_msg.set_type(MessageType::kHardwareInfo);
            net_msg.mutable_hw_info()->set_hw_info(json_msg);
            net_msg.mutable_hw_info()->set_current_cpu_freq(m.hw_info().current_cpu_freq());
            auto data = ProtoAsData(&net_msg);
            module_registry_->BroadcastNetworkMessage(data, true);
        }

    } catch (std::exception& e) {
        LOGE("ParseNetMessage failed: {}", e.what());
    }
}

void WsPanelClient::ProcessCommandEnableModule(const std::string& module_id) {
    if (composition_root_) {
        for (const auto& module : composition_root_->SnapshotModules()) {
            if (module.descriptor.id == module_id) {
                static_cast<void>(composition_root_->SetEnabled(module_id, true));
                return;
            }
        }
    }
    static_cast<void>(module_registry_->SetModuleEnabled(module_id, true));
}

void WsPanelClient::ProcessCommandDisableModule(const std::string& module_id) {
    if (composition_root_) {
        for (const auto& module : composition_root_->SnapshotModules()) {
            if (module.descriptor.id == module_id) {
                static_cast<void>(composition_root_->SetEnabled(module_id, false));
                return;
            }
        }
    }
    static_cast<void>(module_registry_->SetModuleEnabled(module_id, false));
}

} // namespace px
