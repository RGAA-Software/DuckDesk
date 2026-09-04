//
// Created by RGAA on 2024/1/25.
//

#include "network_event_ingress.h"
#include <memory>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include <px_common_new/win32/win_helper.h>

#include "rd_app.h"
#include "rd_context.h"
#include "rd_statistics.h"
#include "px_message.pb.h"
#include "px_render/modules/render_module_registry.h"
#include "app/app_manager.h"
#include "app/app_messages.h"
#include "px_common_new/log.h"
#include "architecture/observers/frame_debugger_observer.h"
#include "architecture/pipeline/encoded_media_bus.h"
#include "architecture/services/input_replay_service.h"
#include "architecture/services/joystick_service.h"
#include "architecture/services/file_transfer_types.h"
#include "architecture/services/file_transfer_service.h"
#include "architecture/services/voice_call_service.h"
#include "px_common_new/data.h"
#include "px_common_new/md5.h"
#include "app_global_messages.h"
#include "settings/rd_settings.h"
#include "network/net_message_maker.h"
#include "px_common_new/process_util.h"
#include "px_render_panel_message.pb.h"
#include "app/win/win_desktop_manager.h"
#include "px_encoder_new/encoder_messages.h"
#include "px_message_new/rp_proto_converter.h"
#include "px_message_new/proto_converter.h"
#include "px_render/network/ws/ws_user_proxy_router.h"
#include "px_render/plugin_interface/px_net_plugin.h"
#include "px_render/modules/module_ids.h"
#include "px_render/network/webrtc/remote/rtc_messages.h"
#include "px_common_new/win32/process_helper.h"
#include "px_common_new/virtual_display_timeouts.h"
#include "px_capture_new/capture_message_maker.h"
#include "px_render/plugin_interface/px_video_encoder_plugin.h"
#include "px_render/plugin_interface/px_monitor_capture_plugin.h"
#include "px_render/plugin_interface/px_stream_plugin.h"

namespace px {

    struct PendingVirtualDisplayRequest {
        std::string device_id;
        std::string stream_id;
        std::chrono::steady_clock::time_point deadline;
        RemoteVirtualDisplayOperation operation = kRemoteVirtualDisplayQuery;
        uint32_t initial_owned_display_count = 0;
        uint64_t initial_topology_generation = 0;
        uint64_t required_capture_epoch = 0;
        std::optional<MsgVirtualDisplayServiceResult> service_result;
    };

    struct CachedVirtualDisplayResponse {
        std::string device_id;
        std::string stream_id;
        VirtualDisplayResponse response;
    };

    struct VirtualDisplayCoordinator {
        std::mutex mutex;
        uint64_t capture_epoch = 0;
        uint64_t first_frame_epoch = 0;
        std::unordered_map<std::string, PendingVirtualDisplayRequest> pending;
        std::unordered_map<std::string, CachedVirtualDisplayResponse> completed;
    };

    void NetworkEventIngress::SendRtcSignalingError(const std::string& stream_id,
                                                      const std::string& code,
                                                      const std::string& message) const {
        if (stream_id.empty() || !module_registry_) {
            return;
        }
        Message response;
        response.set_type(MessageType::kSigAnswerSdpMessage);
        response.mutable_sig_answer_sdp()->set_error_code(code);
        response.mutable_sig_answer_sdp()->set_error_message(message);
        const auto serialized = ProtoAsData(&response);
        module_registry_->SendRelaySignalingMessage(stream_id, serialized);
    }

    namespace {
        int64_t CurrentSystemMilliseconds() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        bool IsControllerOnlyMessage(const MessageType type) {
            return type == MessageType::kMouseEvent
                || type == MessageType::kKeyEvent
                || type == MessageType::kTextInput
                || type == MessageType::kGamepadState
                || type == kReqCtrlAltDelete
                || type == kClipboardInfo
                || type == kClipboardInfoResp
                || type == MessageType::kClipboardReqAtBegin
                || type == MessageType::kClipboardReqAtEnd
                || type == MessageType::kClipboardReqBuffer
                || type == MessageType::kClipboardRespBuffer;
        }

        std::chrono::steady_clock::duration VirtualDisplayServiceResponseTimeout(
            RemoteVirtualDisplayOperation operation) {
            switch (operation) {
                case kRemoteVirtualDisplayQuery:
                    return kVirtualDisplayQueryRenderTimeout;
                case kRemoteVirtualDisplayResetOwned:
                    return kVirtualDisplayResetRenderTimeout;
                case kRemoteVirtualDisplayCreate:
                case kRemoteVirtualDisplayRemoveLast:
                default:
                    return kVirtualDisplayMutationRenderTimeout;
            }
        }

        bool VerifyGuestDeviceCredential(const RdSettings& settings,
                                         const std::string& safety_pwd_md5) {
            if (settings.device_safety_pwd_.empty() && settings.device_random_pwd_.empty()) {
                return true;
            }
            if (safety_pwd_md5.empty()) {
                return false;
            }
            if (!settings.device_safety_pwd_.empty() &&
                settings.device_safety_pwd_ == safety_pwd_md5) {
                return true;
            }
            return !settings.device_random_pwd_.empty() &&
                   MD5::Hex(settings.device_random_pwd_) == safety_pwd_md5;
        }

        void SendVirtualDisplayResponse(
            const std::shared_ptr<RdApplication>& app,
            const std::string& device_id,
            const std::string& stream_id,
            const VirtualDisplayResponse& response) {
            px::Message message;
            message.set_type(kVirtualDisplayResponse);
            message.set_device_id(device_id);
            message.set_stream_id(stream_id);
            *message.mutable_virtual_display_response() = response;
            app->PostNetMessage(ProtoAsData(&message));
        }

        VirtualDisplayResponse BuildVirtualDisplayResponse(
            const MsgVirtualDisplayServiceResult& result,
            VirtualDisplayResponseState state) {
            VirtualDisplayResponse response;
            response.set_request_id(result.request_id_);
            response.set_accepted(result.accepted_);
            response.set_state(state);
            response.set_topology_changed(result.topology_changed_);
            response.set_topology_generation(result.topology_generation_);
            response.set_logical_display_id(result.logical_display_id_);
            response.set_error_code(result.error_code_);
            response.set_error_message(result.error_message_);
            response.set_owned_display_count(result.owned_display_count_);
            response.set_actual_virtual_display_count(result.actual_virtual_display_count_);
            response.set_driver_installed(result.driver_installed_);
            response.set_package_valid(result.package_valid_);
            response.set_removal_safe(result.removal_safe_);
            return response;
        }

        VirtualDisplayResponse BuildVirtualDisplayFailure(
            const std::string& request_id,
            const std::string& code,
            const std::string& message) {
            VirtualDisplayResponse response;
            response.set_request_id(request_id);
            response.set_accepted(false);
            response.set_state(kVirtualDisplayFailed);
            response.set_error_code(code);
            response.set_error_message(message);
            return response;
        }

        void CacheResponseLocked(
            VirtualDisplayCoordinator& coordinator,
            const std::string& request_id,
            CachedVirtualDisplayResponse&& cached) {
            if (coordinator.completed.size() >= 256) {
                coordinator.completed.clear();
            }
            coordinator.completed[request_id] = std::move(cached);
        }

        void CompleteVirtualDisplayRequests(
            const std::shared_ptr<RdApplication>& app,
            const std::shared_ptr<VirtualDisplayCoordinator>& coordinator) {
            std::vector<CachedVirtualDisplayResponse> ready;
            {
                std::scoped_lock lock(coordinator->mutex);
                for (auto it = coordinator->pending.begin(); it != coordinator->pending.end();) {
                    if (!it->second.service_result) {
                        ++it;
                        continue;
                    }
                    const auto& result = *it->second.service_result;
                    const bool capture_ready =
                        coordinator->capture_epoch >= it->second.required_capture_epoch &&
                        coordinator->first_frame_epoch >= it->second.required_capture_epoch;
                    if (result.accepted_ && result.topology_changed_ && !capture_ready) {
                        ++it;
                        continue;
                    }
                    const auto state = !result.accepted_
                        ? kVirtualDisplayFailed
                        : (result.topology_changed_ ? kVirtualDisplayNeedReconnect : kVirtualDisplayReady);
                    CachedVirtualDisplayResponse completed {
                        .device_id = it->second.device_id,
                        .stream_id = it->second.stream_id,
                        .response = BuildVirtualDisplayResponse(result, state),
                    };
                    CacheResponseLocked(*coordinator, it->first, CachedVirtualDisplayResponse(completed));
                    ready.push_back(std::move(completed));
                    it = coordinator->pending.erase(it);
                }
            }
            for (const auto& item : ready) {
                SendVirtualDisplayResponse(app, item.device_id, item.stream_id, item.response);
            }
        }

        void ExpireVirtualDisplayRequests(
            const std::shared_ptr<RdApplication>& app,
            const std::shared_ptr<VirtualDisplayCoordinator>& coordinator) {
            std::vector<CachedVirtualDisplayResponse> expired;
            const auto now = std::chrono::steady_clock::now();
            {
                std::scoped_lock lock(coordinator->mutex);
                for (auto it = coordinator->pending.begin(); it != coordinator->pending.end();) {
                    if (it->second.deadline > now) {
                        ++it;
                        continue;
                    }
                    const auto code = it->second.service_result
                        ? "CAPTURE_REBUILD_TIMEOUT" : "SERVICE_TIMEOUT";
                    const auto message = it->second.service_result
                        ? "display topology changed but capture did not produce a frame in time"
                        : "px_service did not answer the virtual display request in time";
                    CachedVirtualDisplayResponse completed {
                        .device_id = it->second.device_id,
                        .stream_id = it->second.stream_id,
                        .response = BuildVirtualDisplayFailure(it->first, code, message),
                    };
                    CacheResponseLocked(*coordinator, it->first, CachedVirtualDisplayResponse(completed));
                    expired.push_back(std::move(completed));
                    it = coordinator->pending.erase(it);
                }
            }
            for (const auto& item : expired) {
                SendVirtualDisplayResponse(app, item.device_id, item.stream_id, item.response);
            }
        }

        void ReconcileVirtualDisplayRequests(
            const std::shared_ptr<RdApplication>& app,
            const std::shared_ptr<VirtualDisplayCoordinator>& coordinator,
            const MsgVirtualDisplayServiceResult& status) {
            if (!status.accepted_) {
                return;
            }
            bool changed = false;
            {
                std::scoped_lock lock(coordinator->mutex);
                for (auto& [request_id, pending] : coordinator->pending) {
                    if (pending.service_result ||
                        status.topology_generation_ <= pending.initial_topology_generation) {
                        continue;
                    }
                    const bool expected_topology =
                        (pending.operation == kRemoteVirtualDisplayCreate &&
                         status.owned_display_count_ == pending.initial_owned_display_count + 1) ||
                        (pending.operation == kRemoteVirtualDisplayRemoveLast &&
                         pending.initial_owned_display_count > 0 &&
                         status.owned_display_count_ + 1 == pending.initial_owned_display_count) ||
                        (pending.operation == kRemoteVirtualDisplayResetOwned &&
                         status.owned_display_count_ == 0);
                    if (!expected_topology) {
                        continue;
                    }
                    auto reconciled = status;
                    reconciled.request_id_ = request_id;
                    reconciled.topology_changed_ = true;
                    pending.service_result = std::move(reconciled);
                    pending.deadline = std::chrono::steady_clock::now() +
                        kVirtualDisplayCaptureRebuildTimeout;
                    changed = true;
                    LOGW("Reconciled virtual display request {} from authoritative status: "
                         "owned {} -> {}, generation {} -> {}",
                         request_id, pending.initial_owned_display_count,
                         status.owned_display_count_, pending.initial_topology_generation,
                         status.topology_generation_);
                }
            }
            if (changed) {
                CompleteVirtualDisplayRequests(app, coordinator);
            }
        }
    }

    std::shared_ptr<NetworkEventIngress> NetworkEventIngress::Make(
        const std::shared_ptr<RdApplication>& app) {
        auto ingress = std::make_shared<NetworkEventIngress>(app);
        ingress->InitListeners();
        return ingress;
    }

    NetworkEventIngress::NetworkEventIngress(
        const std::shared_ptr<RdApplication>& app)
        : settings_(*RdSettings::Instance()) {
        this->app_ = app;
        this->context_ = app->GetContext();
        this->module_registry_ = app->GetRenderModuleRegistry();
        this->statistics_ = RdStatistics::Instance();
        input_replay_service_ = context_->GetInputReplayService();
        joystick_service_ = context_->GetJoystickService();
        file_transfer_service_ = context_->GetFileTransferService();
        voice_call_service_ = context_->GetVoiceCallService();
        virtual_display_ = std::make_shared<VirtualDisplayCoordinator>();

        msg_notifier_ = this->app_->GetContext()->GetMessageNotifier();
    }

    void NetworkEventIngress::InitListeners() {
        msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kState);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<CaptureMonitorInfoMessage>([weak_self](const CaptureMonitorInfoMessage& msg) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            if (self->input_replay_service_) {
                self->input_replay_service_->UpdateCaptureMonitorInfo(msg);
            }
            {
                std::scoped_lock lock(self->virtual_display_->mutex);
                ++self->virtual_display_->capture_epoch;
            }
            CompleteVirtualDisplayRequests(self->app_, self->virtual_display_);
        });
        msg_listener_->Listen<MsgCaptureTopologyFirstFrame>([weak_self](const MsgCaptureTopologyFirstFrame&) {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            {
                std::scoped_lock lock(self->virtual_display_->mutex);
                self->virtual_display_->first_frame_epoch = self->virtual_display_->capture_epoch;
            }
            CompleteVirtualDisplayRequests(self->app_, self->virtual_display_);
        });
        msg_listener_->Listen<MsgTimer1000>([weak_self](const MsgTimer1000&) {
            if (const auto self = weak_self.lock()) {
                ExpireVirtualDisplayRequests(self->app_, self->virtual_display_);
            }
        });
        msg_listener_->Listen<MsgVirtualDisplayServiceResult>(
            [weak_self](const MsgVirtualDisplayServiceResult& status) {
                if (const auto self = weak_self.lock()) {
                    ReconcileVirtualDisplayRequests(
                        self->app_, self->virtual_display_, status);
                }
            });
    }

    void NetworkEventIngress::ProcessClientConnectedEvent(const std::shared_ptr<PxPluginClientConnectedEvent>& event) {
        // has no effects in plugin mode
        context_->SendAppMessage(MsgInsertIDR {});
        context_->SendAppMessage(MsgRefreshScreen{});
        LOGI("Connection established");
        if (const auto observer = context_->GetFrameDebuggerObserver()) {
            static_cast<void>(observer->SubmitClientConnected());
        }
        if (const auto media_bus = context_->GetEncodedMediaBus()) {
            media_bus->PublishClientConnected(render::MediaClientConnected{
                .visitor_device_id = event->visitor_device_id_,
                .stream_id = event->stream_id_,
                .transport = event->conn_type_,
            });
        }
        if (voice_call_service_) {
            voice_call_service_->HandleClientConnected(
                event->visitor_device_id_, event->stream_id_,
                event->conn_type_, event->plugin_name_);
        }

        for (const auto& capture : {
                 module_registry_->GetDdaCapture(),
                 module_registry_->GetGdiCapture()}) {
            if (capture) {
                capture->OnNewClientConnected(
                    event->visitor_device_id_, event->stream_id_,
                    event->conn_type_);
            }
        }

        module_registry_->InsertIdr();

        // active the password inputting ui
        if (WinHelper::IsSessionLocked()) {
            LOGI("SessionLocked, send a ctrl+alt+delete");
            app_->ReqCtrlAltDelete(event->visitor_device_id_, event->stream_id_);
        }

        // notify
        context_->SendAppMessage(MsgClientConnected {
            .conn_id_ = event->conn_id_,
            .conn_type_ = event->conn_type_,
            .stream_id_ = event->stream_id_,
            .visitor_device_id_ = event->visitor_device_id_,
            .begin_timestamp_ = event->begin_timestamp_,
        });

        // hook 模式：新客户端连接时让 DLL 丢弃积压事件并重置差分基准/修饰键，
        // 否则上一会话的残留会让游戏先看到一个巨大的位移跳变，且积压导致开始几秒无响应
        if (settings_.IsGameHookMode() && settings_.can_be_operated_) {
            auto reset_msg = CaptureResetInputMessage{};
            PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(reset_msg));
        }
        // The game may have rendered once during boot and paused Present before
        // RTC setup completed. Re-encode that valid shared-texture snapshot so
        // an Observer can see the application without sending input first.
        app_->ReplayLatestGameHookFrame();
        if (settings_.IsGameHookMode()) {
            const auto weak_app = std::weak_ptr<RdApplication>(app_);
            for (const auto delay_ms : {250, 1000}) {
                context_->PostDelayTask([weak_app]() {
                    if (const auto app = weak_app.lock()) {
                        app->ReplayLatestGameHookFrame();
                    }
                }, delay_ms);
            }
        }

        // report it
        ReportClientConnected(event);

        // WebRTC 接入时若主管线已是 H265/全彩:浏览器解不了,主动提示
        if (event->conn_type_ == "RTC") {
            const bool full_color = settings_.EnableFullColorMode();
            const bool hevc = full_color
                || statistics_->video_encoder_format_ == Encoder::EncoderFormat::kHEVC;
            if (hevc) {
                const std::string reason = full_color ? "full_color" : "encoder_format";
                LOGW("WebRTC connected while pipeline is H265 ({}), notify client", reason);
                auto tip = NetMessageMaker::MakeVideoCodecChanged(px::VideoType::kNetHevc, full_color, reason);
                static_cast<void>(
                    module_registry_->PostRtcLocalMessage(tip, false));
            }
        }
    }

    void NetworkEventIngress::ProcessClientDisConnectedEvent(const std::shared_ptr<PxPluginClientDisConnectedEvent>& event) {
        if (const auto media_bus = context_->GetEncodedMediaBus()) {
            media_bus->PublishClientDisconnected(render::MediaClientDisconnected{
                .visitor_device_id = event->visitor_device_id_,
                .stream_id = event->stream_id_,
                .transport = event->plugin_name_,
            });
        }
        if (const auto registry = app_->GetLogicalSessionRegistry()) {
            std::string binding_id;
            if (event->plugin_name_ == kNetWebRtcRemoteLibraryId) {
                binding_id = std::string("rtc:") + event->stream_id_;
            }
            else if (event->plugin_name_ == kNetWebRtcLocalLibraryId) {
                binding_id = std::string("rtc-local:") + event->stream_id_;
            }
            if (!binding_id.empty()) {
                const auto closed = registry->CloseBindingById(
                    binding_id, CurrentSystemMilliseconds());
                if (closed.release_controller_input) {
                    ReleaseControllerInput(LogicalSessionInputLease{
                        .logical_session_id = closed.logical_session_id,
                        .binding_id = binding_id,
                        .generation = closed.lease_generation,
                    });
                }
            }
        }
        MsgClientDisconnected msg{};
        msg.conn_id_ = event->conn_id_;
        msg.visitor_device_id_ = event->visitor_device_id_;
        msg.stream_id_ = event->stream_id_;
        msg.end_timestamp_ = event->end_timestamp_;
        msg.duration_ = event->duration_;
        context_->SendAppMessage(msg);

        if (joystick_service_) {
            joystick_service_->HandleClientDisconnected(event->stream_id_);
        }
        if (file_transfer_service_) {
            file_transfer_service_->HandleRouteDisconnected(
                FileTransferRouteDisconnected{
                    .logical_session_id = event->logical_session_id_,
                    .stream_id = event->stream_id_,
                    .transport_id = event->plugin_name_,
                    .connection_id = !event->connection_instance_id_.empty()
                        ? event->connection_instance_id_ : event->conn_id_,
                });
        }
        if (voice_call_service_) {
            voice_call_service_->HandleClientDisconnected(event->stream_id_);
        }

        // report it
        ReportClientDisConnected(event);
    }

    void NetworkEventIngress::ProcessCapturingMonitorInfoEvent(const std::shared_ptr<PxPluginCapturingMonitorInfoEvent>& event) const {
        LOGI("Will Update CaptureMonitorInfo to replayer plugin.");
        app_->UpdateCapturingMonitorInfo();

        // Send monitor changed message
        if (const auto plugin = app_->GetWorkingMonitorCapturePlugin()) {
            const auto cm_msg = CaptureMonitorInfoMessage {
                .monitors_ = plugin->GetCaptureMonitorInfo(),
                .capturing_monitor_name_ = plugin->GetCapturingMonitorName(),
                .virtual_desktop_bound_rectangle_info_ = plugin->GetVirtualDesktopBoundRectangleInfo()
            };
            msg_notifier_->SendAppMessage(cm_msg);
            module_registry_->UpdateRtcLocalCaptureMonitorInfo(cm_msg);
        }
    }

    void NetworkEventIngress::ProcessNetEvent(const std::shared_ptr<PxPluginNetClientEvent>& event) {
        if (event->is_proto_ && event->message_) {
            auto msg = std::make_shared<Message>();
            auto parse_res = msg->ParsePartialFromArray(event->message_->CStr(), event->message_->Size());
            if (!parse_res) {
                std::cout << "NetworkEventIngress HandleMessage parse error" << std::endl;
                return;
            }

            // notify to all plugins (skip EventReplayer SendInput when hook-inner)
            std::optional<LogicalSessionInputLease> input_lease;
            if (IsControllerOnlyMessage(msg->type())) {
                const auto registry = app_->GetLogicalSessionRegistry();
                const auto now_ms = CurrentSystemMilliseconds();
                if (registry && !event->connection_instance_id_.empty()) {
                    // The server-created transport binding, not the client
                    // supplied protobuf stream_id, is the authority for every
                    // controller-only payload. This keeps an old RTC peer from
                    // impersonating a replacement Controller after takeover.
                    input_lease = registry->FindControllerInputLeaseByBinding(
                        event->connection_instance_id_, now_ms);
                }
                if (!input_lease.has_value()) {
                    LOGW("Drop controller-only message without an active controller lease");
                    return;
                }
            }
            const std::string source_plugin_id = !event->source_plugin_id_.empty()
                ? event->source_plugin_id_
                : (event->from_plugin_ ? event->from_plugin_->GetPluginId() : std::string{});
            const std::string source_connection_id = event->connection_instance_id_;
            if (msg->type() == MessageType::kFileAction ||
                msg->type() == MessageType::kFileResponse) {
                const auto registry = app_->GetLogicalSessionRegistry();
                const auto lease = registry && !source_connection_id.empty()
                    ? registry->FindControllerLeaseByBinding(
                          source_connection_id, CurrentSystemMilliseconds())
                    : std::optional<LogicalSessionInputLease>{};
                if (!lease.has_value()) {
                    LOGW("Drop file-transfer message without an active controller lease");
                    return;
                }
                if (file_transfer_service_) {
                    file_transfer_service_->HandleInbound(FileTransferInbound{
                        .message = msg,
                        .logical_session_id = lease->logical_session_id,
                        .transport_id = source_plugin_id,
                        .connection_id = source_connection_id,
                    });
                }
                return;
            }
            if (msg->type() == MessageType::kVoiceCallRequest ||
                msg->type() == MessageType::kVoiceCallResponse ||
                msg->type() == MessageType::kVoiceAudioConfig ||
                msg->type() == MessageType::kVoiceAudioFrame) {
                if (voice_call_service_) {
                    voice_call_service_->HandleMessage(msg);
                }
                return;
            }
            if (joystick_service_) {
                joystick_service_->HandleMessage(msg);
            }
            const auto weak_self = weak_from_this();
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            // Standard RTC uses the same media engine as Direct RTC. The
            // legacy net_rtc transport only had data channels and could
            // report Connected while delivering no video/audio tracks.
            if (msg->type() == MessageType::kSigOfferSdpMessage) {
                        const auto sub = msg->sig_offer_sdp();
                        const auto stream_id = msg->stream_id();
                        const auto device_id = msg->device_id();
                        const auto sdp = sub.sdp();
                        if (sub.connection_ticket().empty()) {
                            if (!VerifyGuestDeviceCredential(
                                    self->settings_, sub.safety_pwd_md5())) {
                                LOGW("Reject guest full RTC offer: device password mismatch");
                                return;
                            }
                            const auto registry = self->app_->GetLogicalSessionRegistry();
                            if (!registry || stream_id.empty()) {
                                LOGW("Reject direct RTC offer without a local logical-session registry");
                                return;
                            }
                            if (sub.client_nonce().empty()) {
                                LOGW("Reject direct RTC offer without client nonce");
                                return;
                            }
                            // Direct RTC has no Console issuer, but it still
                            // enters the same lease registry after the
                            // device-local password check. The client nonce
                            // makes SDP re-offers a reconnect of one direct
                            // session instead of trusting a caller-selected
                            // stream id as its identity.
                            const auto now_ms = CurrentSystemMilliseconds();
                            const auto direct_session_key = MD5::Hex(
                                device_id + "|" + stream_id + "|" + sub.client_nonce());
                            const auto direct_admission = registry->Bind(
                                {.logical_session_id = std::string("direct:") + direct_session_key,
                                 .stream_id = stream_id,
                                 .subject_id = std::string("direct:") + direct_session_key,
                                 .join_mode = "control",
                                 .expires_at_ms = now_ms + std::chrono::minutes(15).count() * 60 * 1000,
                                 .allow_observer = true,
                                 .allow_takeover = true},
                                LogicalSessionTransport::kRtcLocal,
                                std::string("rtc-local:") + stream_id,
                                sub.takeover(), now_ms);
                            if (direct_admission.code != LogicalSessionAdmissionCode::kAccepted) {
                                LOGW("Reject direct RTC offer: controller lease is occupied");
                                return;
                            }
                            if (direct_admission.release_previous_controller_input) {
                                self->ReleaseControllerInput(LogicalSessionInputLease{
                                    .logical_session_id = direct_admission.previous_controller_session_id,
                                    .generation = direct_admission.previous_controller_lease_generation,
                                });
                                const auto previous_stream = registry->FindStreamId(
                                    direct_admission.previous_controller_session_id);
                                if (previous_stream.has_value()) {
                                    const auto update = PxLogicalSessionCapabilityUpdate{
                                        .stream_id_ = *previous_stream,
                                        .permissions_ = {"view", "audio"},
                                    };
                                    self->module_registry_->ApplyLogicalSessionCapabilities(
                                        update);
                                }
                            }
                            // A guest has no Console ticket from which to mint
                            // temporary TURN credentials. An empty server list
                            // still selects standard RTC and permits host ICE;
                            // managed TURN remains available to ticketed users.
                            self->module_registry_->ApplyRtcLocalRemoteSdp(MsgRtcRemoteSdp {
                                .stream_id_ = stream_id,
                                .device_id_ = device_id,
                                .sdp_ = sdp,
                                .ice_config_json_ = R"({"ice_servers":[]})",
                                .permissions_ = {"view", "input", "clipboard", "file", "audio"},
                            });
                            return;
                        }
                        if (sub.client_nonce().empty()) {
                            LOGW("Reject ticketed full RTC offer without client nonce");
                            return;
                        }
                        self->app_->RedeemConnectionTicket(
                            sub.connection_ticket(), sub.client_nonce(), sub.instance_id(),
                            [weak_self, stream_id, device_id, sdp, takeover = sub.takeover()](
                                bool ok, const std::string& code,
                                const std::vector<std::string>& permissions,
                                const std::string& ice_config_json,
                                const std::string& logical_session_id,
                                const std::string& ticket_stream_id,
                                const std::string& join_mode,
                                const std::string& subject_id,
                                const int64_t expires_at_ms,
                                const bool allow_observer,
                                const bool allow_takeover) {
                                const auto self = weak_self.lock();
                                if (!self) {
                                    return;
                                }
                                const bool may_view = std::find(permissions.begin(), permissions.end(), "view")
                                    != permissions.end();
                                const bool may_file = std::find(permissions.begin(), permissions.end(), "file")
                                    != permissions.end();
                                if (!ok || (!may_view && !may_file) || ice_config_json.empty()) {
                                    LOGW("Reject full RTC ticket: code={}, config_available={}", code,
                                         !ice_config_json.empty());
                                    return;
                                }
                                if (stream_id != ticket_stream_id) {
                                    LOGW("Reject full RTC ticket: stream mismatch");
                                    return;
                                }
                                const auto registry = self->app_->GetLogicalSessionRegistry();
                                if (!registry) {
                                    LOGW("Reject full RTC ticket: logical-session registry unavailable");
                                    return;
                                }
                                const auto admission = registry->Bind(
                                    {.logical_session_id = logical_session_id,
                                     .stream_id = ticket_stream_id,
                                     .subject_id = subject_id,
                                     .join_mode = join_mode,
                                     .expires_at_ms = expires_at_ms,
                                    .allow_observer = allow_observer,
                                    .allow_takeover = allow_takeover},
                                    LogicalSessionTransport::kRtcLocal,
                                    std::string("rtc-local:") + ticket_stream_id,
                                    takeover, CurrentSystemMilliseconds());
                                if (admission.code != LogicalSessionAdmissionCode::kAccepted) {
                                    const bool occupied = admission.code == LogicalSessionAdmissionCode::kOccupied;
                                    LOGW("Reject full RTC ticket: logical-session admission denied, occupied={}", occupied);
                                    self->SendRtcSignalingError(
                                        stream_id,
                                        occupied ? "RTC_OCCUPIED" : "RTC_ACCESS_DENIED",
                                        occupied ? "Remote controller is occupied" : "Remote session admission denied");
                                    return;
                                }
                                if (admission.release_previous_controller_input) {
                                    self->ReleaseControllerInput(LogicalSessionInputLease{
                                        .logical_session_id = admission.previous_controller_session_id,
                                        .generation = admission.previous_controller_lease_generation,
                                    });
                                    const auto previous_stream = registry->FindStreamId(
                                        admission.previous_controller_session_id);
                                    if (previous_stream.has_value()) {
                                        const auto update = PxLogicalSessionCapabilityUpdate{
                                            .stream_id_ = *previous_stream,
                                            .permissions_ = {"view", "audio"},
                                        };
                                        self->module_registry_->ApplyLogicalSessionCapabilities(
                                            update);
                                    }
                                }
                                self->module_registry_->ApplyRtcLocalRemoteSdp(MsgRtcRemoteSdp {
                                    .stream_id_ = stream_id,
                                    .device_id_ = device_id,
                                    .sdp_ = sdp,
                                    .ice_config_json_ = ice_config_json,
                                    .permissions_ = permissions,
                                });
                            });
                        return;
                    }
            if (msg->type() == MessageType::kSigIceMessage) {
                        const auto sub = msg->sig_ice();
                        self->module_registry_->ApplyRtcLocalRemoteIce(MsgRtcRemoteIce {
                            .stream_id_ = msg->stream_id(),
                            .device_id_ = msg->device_id(),
                            .ice_ = sub.ice(),
                            .mid_ = sub.mid(),
                            .sdp_mline_index_ = sub.sdp_mline_index(),
                        });
                        return;
            }
            self->module_registry_->DispatchRtcLocalMessage(msg);

#if PX_USER_PROXY_ENABLED
            if (msg->type() == MessageType::kClipboardInfo) {
                LOGI("[LAT-clip] render recv kClipboardInfo, type: {}, files: {}, len: {}", (int)msg->clipboard_info().type(), msg->clipboard_info().files_size(), event->message_->Size());
                context_->PostTask([weak_self, msg, event]() {
                    const auto self = weak_self.lock();
                    if (!self) {
                        return;
                    }
                    if (!self->module_registry_->IsWsUserProxyConnected()) {
                        LOGW("user-proxy not connected, drop client clipboard, type={}, len={}",
                             (int)msg->clipboard_info().type(), event->message_->Size());
                        return;
                    }
                    pxrp::RpMessage rp_msg;
                    rp_msg.set_type(pxrp::kRpRawRenderMessage);
                    auto sub = rp_msg.mutable_raw_render_msg();
                    sub->set_msg(event->message_->AsString());
                    sub->set_data_channel(false);
                    sub->set_stream_id(msg->stream_id());
                    sub->set_device_id(msg->device_id());
                    LOGI("PostUserProxyMessage client clipboard, type={}, len={}",
                         (int)msg->clipboard_info().type(), event->message_->Size());
                    self->app_->PostUserProxyMessage(RpProtoAsData(&rp_msg));
                });
            } else if (msg->type() == MessageType::kClipboardReqBuffer ||
                       msg->type() == MessageType::kClipboardRespBuffer) {
                LOGI("[LAT-clip] render recv clipboard buffer msg, type: {}, len: {}",
                     (int)msg->type(), event->message_->Size());
                context_->PostTask([weak_self, msg, event]() {
                    const auto self = weak_self.lock();
                    if (!self) {
                        return;
                    }
                    if (!self->module_registry_->IsWsUserProxyConnected()) {
                        LOGW("user-proxy not connected, drop clipboard buffer msg, type={}, len={}",
                             (int)msg->type(), event->message_->Size());
                        return;
                    }
                    pxrp::RpMessage rp_msg;
                    rp_msg.set_type(pxrp::kRpRawRenderMessage);
                    auto sub = rp_msg.mutable_raw_render_msg();
                    sub->set_msg(event->message_->AsString());
                    sub->set_data_channel(true);
                    sub->set_stream_id(msg->stream_id());
                    sub->set_device_id(msg->device_id());
                    LOGI("PostUserProxyMessage clipboard buffer msg, type={}, len={}",
                         (int)msg->type(), event->message_->Size());
                    self->app_->PostUserProxyMessage(RpProtoAsData(&rp_msg));
                });
            }
#else
            // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
            // notify to panel
            context_->PostTask([weak_self, event]() {
                const auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                pxrp::RpMessage msg;
                msg.set_type(pxrp::kRpRawRenderMessage);
                auto sub = msg.mutable_raw_render_msg();
                sub->set_msg(event->message_->AsString());
                auto buffer = RpProtoAsData(&msg);
                self->app_->PostPanelMessage(buffer);
            });
#endif

            switch (msg->type()) {
                case kHello: {
                    this->ProcessHelloEvent(std::move(msg));
                    if (event->nt_plugin_type_ == NetPluginType::kUdpKcp) {
                        this->SyncInfoToUdpPlugin(event->socket_fd_, msg->device_id(), msg->stream_id());
                    }
                    break;
                }
                case kAck: {
                    this->ProcessAck(event, msg);
                    break;
                }
                case kHeartBeat: {
                    ProcessHeartBeat(std::move(msg));
                    if (event->nt_plugin_type_ == NetPluginType::kUdpKcp) {
                        this->SyncInfoToUdpPlugin(event->socket_fd_, msg->device_id(), msg->stream_id());
                    }
                    break;
                }
                case MessageType::kClientStatistics: {
                    ProcessClientStatistics(std::move(msg));
                    break;
                }
                case kClipboardInfo: {
                    const auto& sub = msg->clipboard_info();
                    LOGI("Clipboard msg: {}", sub.msg());
                    LOGI("Clipboard files: {}", sub.files_size());
                    for (const auto& file : sub.files()) {
                        LOGI("File: {}", file.full_path());
                    }
                    break;
                }
                case kClipboardInfoResp: {
                    break;
                }
                case kSwitchMonitor: {
                    ProcessSwitchMonitor(std::move(msg));
                    break;
                }
                case kSwitchWorkMode: {
                    ProcessSwitchWorkMode(std::move(msg));
                    break;
                }
                case kChangeMonitorResolution: {
                    ProcessChangeMonitorResolution(std::move(msg));
                    break;
                }
                case kInsertKeyFrame: {
                    ProcessInsertKeyFrame(std::move(msg));
                    break;
                }
                case kReqCtrlAltDelete: {
                    LOGW("Request the CtrlAltDelete");
                    ProcessCtrlAltDelete(std::move(msg));
                    break;
                }
                case MessageType::kClipboardReqAtBegin:
                case MessageType::kClipboardReqAtEnd:
                case MessageType::kClipboardReqBuffer:
                case MessageType::kClipboardRespBuffer: {
                    //if (auto plugin = module_registry_->GetClipboardPlugin(); plugin) {
                    //    plugin->OnMessage(msg);
                    //}
                    break;
                }
                case MessageType::kUpdateDesktop: {
                    ProcessUpdateDesktop();
                    break;
                }
                case MessageType::kHardUpdateDesktop: {
                    ProcessHardUpdateDesktop();
                    break;
                }
                case kSwitchFullColorMode: {
                    ProcessSwitchFullColorMode(std::move(msg));
                    break;
                }
                case kStartMediaRecordClientSide: {
                    ProcessStartMediaRecordClientSide();
                    break;
                }
                case kStopMediaRecordClientSide: {
                    ProcessStopMediaRecordClientSide();
                    break;
                }
                case kModifyFps: {
                    ProcessModifyFps(std::move(msg));
                    break;
                }
                case kVirtualDisplayRequest: {
                    ProcessVirtualDisplayRequest(std::move(msg));
                    break;
                }
                case MessageType::kMouseEvent: {
                    if (settings_.app_.IsGlobalReplayMode() &&
                        input_replay_service_) {
                        input_replay_service_->HandleMessage(msg);
                    }
                    else {
                        ProcessMouseEvent(std::move(msg), *input_lease);
                    }
                    break;
                }
                case MessageType::kKeyEvent: {
                    if (settings_.app_.IsGlobalReplayMode() &&
                        input_replay_service_) {
                        input_replay_service_->HandleMessage(msg);
                    }
                    else {
                        ProcessKeyboardEvent(std::move(msg), *input_lease);
                    }
                    break;
                }
                case MessageType::kTextInput: {
                    ProcessTextInput(std::move(msg));
                    break;
                }
                case kFocusOutEvent:
                case kExitControlledEnd: {
                    if (settings_.app_.IsGlobalReplayMode() &&
                        input_replay_service_) {
                        input_replay_service_->HandleMessage(msg);
                    }
                    break;
                }
                case kStopRender: {
                    // A game-hook render owns the game in a kill-on-close job.
                    // A browser opening a stream must never be able to end that
                    // game through a control-packet mismatch. Console owns the
                    // lifecycle of game-hook instances and sends kSrvStopServer
                    // over its authenticated service channel instead.
                    LOGW("kStopRender received from client: device={}, stream={}",
                         msg->device_id(), msg->stream_id());
                    if (settings_.IsGameHookMode() || settings_.IsWebViewMode()) {
                        LOGW("Ignore client kStopRender for Console application instance; use Console stop action instead.");
                        break;
                    }
                    ProcessUtil::KillProcess(GetCurrentProcessId());
                    break;
                }
                case kLockDevice: {
                    context_->SendAppMessage(MsgPanelStreamLockScreen{});
                }
                default: {
                   
                }
            }
        } else {

        }
    }

    void NetworkEventIngress::ProcessHelloEvent(std::shared_ptr<Message>&& msg) {
        const auto& hello = msg->hello();
        auto event = MsgClientHello();
        event.device_id_ = msg->device_id();
        event.stream_id_ = msg->stream_id();
        event.enable_audio_ = hello.enable_audio();
        event.enable_video_ = hello.enable_video();
        event.enable_controller = hello.enable_controller();
        event.client_type_ = hello.client_type();
        event.device_name_ = hello.device_name();
        app_->GetContext()->SendAppMessage(event);

        auto e = std::make_shared<MsgClientHello>(event);
        module_registry_->DispatchNetworkAppEvent(e);
    }

    NetworkEventIngress::InputLeaseKey NetworkEventIngress::ToInputLeaseKey(
        const LogicalSessionInputLease& lease) {
        return InputLeaseKey{
            .logical_session_id_ = lease.logical_session_id,
            .generation_ = lease.generation,
        };
    }

    void NetworkEventIngress::ReleaseControllerInput(const LogicalSessionInputLease& lease) {
        const auto key = ToInputLeaseKey(lease);
        const auto found = input_states_.find(key);
        if (found == input_states_.end()) {
            return;
        }
        auto input_state = std::move(found->second);
        input_states_.erase(found);
        if (!settings_.can_be_operated_
            || (input_state.pressed_keys_.empty() && input_state.pressed_mouse_buttons_.empty())) {
            return;
        }
        if (settings_.app_.IsGlobalReplayMode()) {
            // The EventReplayer owns the system SendInput state.  Focus-out is
            // its value-only plug-in command for releasing all pressed keys
            // and mouse buttons; it must be sent only after this generation is
            // removed from input_states_.
            if (input_replay_service_) {
                input_replay_service_->ReleaseInputState();
            }
            LOGI("controller desktop input released: session={}, generation={}",
                 lease.logical_session_id, lease.generation);
            return;
        }
        if (settings_.IsWebViewMode()) {
            for (const auto key_code : input_state.pressed_keys_) {
                KeyEvent event;
                event.set_key_code(key_code);
                event.set_down(false);
                app_->SendWebViewKeyEvent(event);
            }
            // CEF clears any internal pointer capture and IME composition on
            // focus loss. This is the WebView equivalent of releasing a
            // desktop lease and does not transfer focus to an Observer.
            app_->SendWebViewFocusEvent(false);
            LOGI("controller WebView input released: session={}, generation={}, keys={}, mouse_buttons={}",
                 lease.logical_session_id, lease.generation, input_state.pressed_keys_.size(),
                 input_state.pressed_mouse_buttons_.size());
            return;
        }
        const auto hwnd = app_->GetAppManager()->GetWindowHandle();
        if (!hwnd || !IsWindow(static_cast<HWND>(hwnd))) {
            LOGW("controller lease released without a valid game HWND: session={}",
                 lease.logical_session_id);
            return;
        }
        const auto hwnd_value = reinterpret_cast<uint64_t>(hwnd);
        for (const auto key_code : input_state.pressed_keys_) {
            const auto message = CaptureMessageMaker::MakeKeyboardEventMessage(
                hwnd_value, key_code, 0, 0, 0);
            PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(message));
        }
        for (const auto button : input_state.pressed_mouse_buttons_) {
            const auto message = CaptureMessageMaker::MakeMouseEventMessage(
                hwnd_value, input_state.last_mouse_x_, input_state.last_mouse_y_, button, 0, false, true);
            PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(message));
        }
        LOGI("controller lease released: session={}, generation={}, keys={}, mouse_buttons={}",
             lease.logical_session_id, lease.generation, input_state.pressed_keys_.size(),
             input_state.pressed_mouse_buttons_.size());
    }

    void NetworkEventIngress::ProcessMouseEvent(
        std::shared_ptr<Message>&& msg, const LogicalSessionInputLease& lease) {
        if (!settings_.can_be_operated_) {
            return;
        }
        const auto& mouse_event = msg->mouse_event();
        auto& input_state = input_states_[ToInputLeaseKey(lease)];
        if (mouse_event.pressed()) {
            input_state.pressed_mouse_buttons_.insert(mouse_event.button());
        } else if (mouse_event.released()) {
            input_state.pressed_mouse_buttons_.erase(mouse_event.button());
        }
        if (settings_.app_.IsGlobalReplayMode()) {
            // Desktop: EventReplayerPlugin handles the actual SendInput via
            // OnMessage; the router still owns lease-scoped state above.
            return;
        }
        if (settings_.IsWebViewMode()) {
            app_->SendWebViewMouseEvent(mouse_event);
            return;
        }
        auto hwnd = this->app_->GetAppManager()->GetWindowHandle();
        auto hwnd_ptr = reinterpret_cast<uint64_t>(hwnd);
        if (!hwnd || !IsWindow(static_cast<HWND>(hwnd))) {
            static thread_local uint64_t s_n = 0;
            if (++s_n == 1 || (s_n % 100) == 0) {
                LOGW("hook-inner mouse: no game HWND yet, drop n={}", s_n);
            }
            return;
        }
        RECT rect{0, 0, 0, 0};
        if (!ProcessHelper::GetWindowPositionByHwnd(static_cast<HWND>(hwnd), rect)) {
            LOGE("GetWindowPositionByHwnd failed for HWND: {:x}", hwnd_ptr);
            return;
        }

        int app_width = rect.right - rect.left;
        int app_height = rect.bottom - rect.top;
        if (app_width <= 0 || app_height <= 0) {
            LOGW("hook-inner mouse: invalid window size {}x{}", app_width, app_height);
            return;
        }

        auto x = rect.left + app_width * mouse_event.x_ratio();
        auto y = rect.top + app_height * mouse_event.y_ratio();

        // 记录当前按下的鼠标键 / 最近一次坐标，用于该 Controller lease
        // 断开或被接管时补发释放事件。
        if (mouse_event.pressed()) {
            input_state.pressed_mouse_buttons_.insert(mouse_event.button());
        } else if (mouse_event.released()) {
            input_state.pressed_mouse_buttons_.erase(mouse_event.button());
        }
        input_state.last_mouse_x_ = static_cast<int>(x);
        input_state.last_mouse_y_ = static_cast<int>(y);

        auto mouse_event_msg = CaptureMessageMaker::MakeMouseEventMessage(
            hwnd_ptr, (int)x, (int)y, mouse_event.button(), mouse_event.data(),
            mouse_event.pressed(), mouse_event.released());
        {
            static thread_local uint64_t s_n = 0;
            const auto n = ++s_n;
            if (n <= 5 || (n % 200) == 0) {
                LOGI("hook-inner mouse: n={} hwnd={:x} screen=({},{}) ratio=({:.3f},{:.3f}) btn={}",
                     n, hwnd_ptr, (int)x, (int)y, mouse_event.x_ratio(), mouse_event.y_ratio(),
                     mouse_event.button());
            }
        }
        PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(mouse_event_msg));
    }

    void NetworkEventIngress::ProcessKeyboardEvent(
        std::shared_ptr<Message>&& msg, const LogicalSessionInputLease& lease) {
        if (!settings_.can_be_operated_) {
            return;
        }
        const auto& key_event = msg->key_event();
        auto& input_state = input_states_[ToInputLeaseKey(lease)];
        if (key_event.down()) {
            input_state.pressed_keys_.insert(key_event.key_code());
        } else {
            input_state.pressed_keys_.erase(key_event.key_code());
        }
        if (settings_.app_.IsGlobalReplayMode()) {
            return;
        }
        if (settings_.IsWebViewMode()) {
            app_->SendWebViewKeyEvent(key_event);
            return;
        }
        auto hwnd = this->app_->GetAppManager()->GetWindowHandle();
        auto hwnd_ptr = reinterpret_cast<uint64_t>(hwnd);
        if (!hwnd || !IsWindow(static_cast<HWND>(hwnd))) {
            static thread_local uint64_t s_n = 0;
            if (++s_n == 1 || (s_n % 100) == 0) {
                LOGW("hook-inner key: no game HWND yet, drop n={}", s_n);
            }
            return;
        }

        // 记录当前按下的键，用于该 Controller lease 断开或被接管时补发释放事件。
        if (key_event.down()) {
            input_state.pressed_keys_.insert(key_event.key_code());
        } else {
            input_state.pressed_keys_.erase(key_event.key_code());
        }

        auto keyboard_msg = CaptureMessageMaker::MakeKeyboardEventMessage(
            hwnd_ptr, key_event.key_code(), key_event.down(), key_event.num_lock_status(),
            key_event.caps_lock_status());
        {
            static thread_local uint64_t s_n = 0;
            const auto n = ++s_n;
            if (n <= 5 || (n % 200) == 0) {
                LOGI("hook-inner key: n={} hwnd={:x} key=0x{:x} down={}",
                     n, hwnd_ptr, key_event.key_code(), key_event.down());
            }
        }
        PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(keyboard_msg));
    }

    void NetworkEventIngress::ProcessTextInput(std::shared_ptr<Message>&& msg) {
        if (!settings_.can_be_operated_ ||
            settings_.GetInputTarget() != InputTarget::kCefBrowser) {
            return;
        }
        const auto& input = msg->text_input();
        if (input.text().empty() || input.text().size() > 4096) {
            return;
        }
        app_->SendWebViewTextInput(input);
    }

    void NetworkEventIngress::PostIpcMessage(const std::string& msg) {
        const auto weak_self = weak_from_this();
        auto task_msg = AppMessageMaker::MakeTaskMessage([weak_self, msg]() {
            if (const auto self = weak_self.lock()) {
                self->app_->PostIpcMessage(msg);
            }
        });
        app_->PostGlobalAppMessage(std::move(task_msg));
    }

    void NetworkEventIngress::ProcessClientStatistics(std::shared_ptr<Message>&& msg) {
        auto& cst = msg->client_statistics();
        statistics_->CopyDecodeDurations(cst.decode_durations());
        statistics_->CopyClientVideoRecvGaps(cst.video_recv_gaps());
        statistics_->client_fps_video_recv_ = cst.fps_video_recv();
        statistics_->client_fps_render_ = cst.fps_render();
        statistics_->client_recv_media_data_ = cst.recv_media_data();
        statistics_->render_width_ = cst.render_width();
        statistics_->render_height_ = cst.render_height();
    }

    void NetworkEventIngress::ProcessHeartBeat(std::shared_ptr<Message>&& msg) {
        const auto weak_self = weak_from_this();
        app_->PostGlobalTask([weak_self, msg]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            auto& hb = msg->heartbeat();
            auto proto_msg = NetMessageMaker::MakeOnHeartBeatMsg(self->app_, hb.index(), hb.timestamp());
            self->app_->PostNetMessage(proto_msg);
        });

        auto event = std::make_shared<MsgClientHeartbeat>();
        event->device_id_ = msg->device_id();
        event->stream_id_ = msg->stream_id();
        event->hb_index_ = msg->heartbeat().index();
        event->timestamp_ = msg->heartbeat().timestamp();
        module_registry_->DispatchNetworkAppEvent(event);
    }

    void NetworkEventIngress::ProcessClipboardInfo(std::shared_ptr<Message>&& msg) {
        //if (auto plugin = module_registry_->GetClipboardPlugin(); plugin) {
        //    plugin->OnMessage(msg);
        //}
    }

    void NetworkEventIngress::ProcessSwitchMonitor(std::shared_ptr<Message>&& msg) {
        LOGI("ProcessSwitchMonitor, name: {}", msg->switch_monitor().name());
        const auto weak_self = weak_from_this();
        app_->PostGlobalTask([weak_self, msg]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            auto sm = msg->switch_monitor();
            auto capture_plugin = self->app_->GetWorkingMonitorCapturePlugin();
            if (!capture_plugin) {
                return;
            }
            capture_plugin->SetCaptureMonitor(sm.name());
            //plugin->SendCapturingMonitorMessage();

            auto encoder_plugins = self->app_->GetWorkingVideoEncoderPlugins();
            for (const auto& [k, encoder_plugin] : encoder_plugins) {
                if (encoder_plugin) {
                    encoder_plugin->InsertIdr();
                }
            }

            // capturing monitor info
            self->app_->UpdateCapturingMonitorInfo();

            int mon_index = 0;
            auto mon_index_res = capture_plugin->GetMonIndexByName(sm.name());
            if (mon_index_res.has_value()) {
                mon_index = mon_index_res.value();
            }
            auto proto_msg = NetMessageMaker::MakeMonitorSwitched(sm.name(), mon_index);
            self->app_->PostNetMessage(proto_msg);
        });
    }

    void NetworkEventIngress::ProcessSwitchWorkMode(std::shared_ptr<Message>&& msg) {
        const auto weak_self = weak_from_this();
        app_->PostGlobalTask([weak_self, msg]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            auto wm = msg->work_mode();
            auto plugin = self->app_->GetWorkingMonitorCapturePlugin();
            if (!plugin) {
                LOGE("Working monitor capture is empty!");
                return;
            }
            if (wm.mode() == SwitchWorkMode::kWork) {
                plugin->SetCaptureFps(30);
            } else if (wm.mode() == SwitchWorkMode::kGame) {
                plugin->SetCaptureFps(60);
            }
        });
    }

    void NetworkEventIngress::ProcessSwitchFullColorMode(std::shared_ptr<Message>&& msg) {
        auto sw = msg->switch_full_color_mode();
        settings_.SetFullColorMode(sw.enable());
    }

    void NetworkEventIngress::ProcessStartMediaRecordClientSide() {
        auto encoder_plugins = app_->GetWorkingVideoEncoderPlugins();
        for (const auto& [k, encoder_plugin] : encoder_plugins) {
            if (encoder_plugin) {
                encoder_plugin->InsertIdr();
                encoder_plugin->SetClientSideMediaRecording(true);
            }
        }
    }

    void NetworkEventIngress::ProcessStopMediaRecordClientSide() {
        auto encoder_plugins = app_->GetWorkingVideoEncoderPlugins();
        for (const auto& [k, encoder_plugin] : encoder_plugins) {
            if (encoder_plugin) {
                encoder_plugin->SetClientSideMediaRecording(false);
            }
        }
    }

    void NetworkEventIngress::ProcessChangeMonitorResolution(std::shared_ptr<Message>&& msg) {
        const auto weak_self = weak_from_this();
        app_->PostGlobalTask([weak_self, msg]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            auto cmr = msg->change_monitor_resolution();
            self->app_->ResetMonitorResolution(cmr.monitor_name(), cmr.target_width(), cmr.target_height());
        });
    }

    void NetworkEventIngress::ProcessInsertKeyFrame(std::shared_ptr<Message>&&) {
        const auto weak_self = weak_from_this();
        app_->PostGlobalTask([weak_self]() {
            if (const auto self = weak_self.lock()) {
                self->app_->SendAppMessage(MsgInsertKeyFrame{});
            }
        });
    }

    void NetworkEventIngress::ProcessEncodedAudioFrameEvent(const std::shared_ptr<Data>& data, int samples, int channels, int bits, int frame_size) {
        auto net_msg = NetMessageMaker::MakeAudioFrameMsg(data, samples, channels, bits, frame_size);
        //statistics_->AppendMediaBytes(net_msg.size());
        app_->PostNetMessage(net_msg);

        if (data) {
            if (const auto media_bus = context_->GetEncodedMediaBus();
                media_bus && media_bus->NeedsEncodedAudio()) {
                media_bus->PublishEncodedAudio(
                    std::make_shared<const render::EncodedAudioFrame>(
                        render::EncodedAudioFrame{
                            .timestamp_us = static_cast<std::uint64_t>(
                                TimeUtil::GetCurrentTimestamp()) * 1000U,
                            .codec = "opus",
                            .samples = static_cast<std::uint32_t>(samples),
                            .channels = static_cast<std::uint16_t>(channels),
                            .bits_per_sample = static_cast<std::uint16_t>(bits),
                            .frame_size = static_cast<std::uint32_t>(frame_size),
                            .payload = render::MakeImmutableByteBuffer(
                                data->AsString()),
                        }));
            }
        }

    }

    void NetworkEventIngress::ProcessCtrlAltDelete(std::shared_ptr<Message>&& msg) {
        app_->ReqCtrlAltDelete(msg->device_id(), msg->stream_id());
    }

    void NetworkEventIngress::ProcessUpdateDesktop() {
        if (context_) {
            context_->SendAppMessage(MsgRefreshScreen{});
        }
    }

    void NetworkEventIngress::ProcessHardUpdateDesktop() {
        auto desk_manager = app_->GetDesktopManager();
        if (!desk_manager) {
            return;
        }
        desk_manager->UpdateDesktop();
    }

    void NetworkEventIngress::SyncInfoToUdpPlugin(int64_t socket_fd, const std::string& device_id, const std::string& stream_id) {
        module_registry_->SyncUdpInfo(socket_fd, device_id, stream_id);
    }

    void NetworkEventIngress::ProcessRtcReportEvent(const std::shared_ptr<PxPluginRtcReportEvent>& event) {

    }

    void NetworkEventIngress::ReportClientConnected(const std::shared_ptr<PxPluginClientConnectedEvent>& event) {
        const auto weak_self = weak_from_this();
        app_->PostGlobalTask([weak_self, event]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            pxrp::RpMessage msg;
            msg.set_type(pxrp::kRpClientConnected);
            auto sub = msg.mutable_client_connected();
            sub->set_conn_id(event->conn_id_);
            sub->set_stream_id(event->stream_id_);
            sub->set_conn_type(event->conn_type_);
            sub->set_visitor_device_id(event->visitor_device_id_);
            sub->set_begin_timestamp(event->begin_timestamp_);
            auto buffer = RpProtoAsData(&msg);
            self->app_->PostPanelMessage(buffer);
        });
    }

    void NetworkEventIngress::ReportClientDisConnected(const std::shared_ptr<PxPluginClientDisConnectedEvent>& event) {
        const auto weak_self = weak_from_this();
        app_->PostGlobalTask([weak_self, event]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            pxrp::RpMessage msg;
            msg.set_type(pxrp::kRpClientDisConnected);
            auto sub = msg.mutable_client_disconnected();
            sub->set_conn_id(event->conn_id_);
            sub->set_stream_id(event->stream_id_);
            sub->set_visitor_device_id(event->visitor_device_id_);
            sub->set_end_timestamp(event->end_timestamp_);
            sub->set_duration(event->duration_);
            auto buffer = RpProtoAsData(&msg);
            self->app_->PostPanelMessage(buffer);
        });
    }

    // client -> render 修改帧率
    void NetworkEventIngress::ProcessModifyFps(std::shared_ptr<Message>&& msg) {
        auto mf = msg->modify_fps();
        int fps = mf.fps();
        if (context_) {
            context_->SendAppMessage(MsgModifyFps{.fps_ = fps});
        }
    }

    void NetworkEventIngress::ProcessVirtualDisplayRequest(std::shared_ptr<Message>&& msg) {
        const auto& request = msg->virtual_display_request();
        const auto request_id = request.request_id();
        const auto device_id = msg->device_id();
        const auto stream_id = msg->stream_id();

        const auto app = app_;
        const auto fail = [app, &device_id, &stream_id, &request_id](
                              const std::string& code, const std::string& message) {
            SendVirtualDisplayResponse(
                app, device_id, stream_id,
                BuildVirtualDisplayFailure(request_id, code, message));
        };
        if (request_id.empty() || request.operation() < kRemoteVirtualDisplayCreate ||
            request.operation() > kRemoteVirtualDisplayResetOwned) {
            fail("INVALID_ARGUMENT", "invalid virtual display request");
            return;
        }
        if (!settings_.can_be_operated_) {
            fail("PERMISSION_DENIED", "the session is view-only");
            return;
        }
        if (!settings_.virtual_display_enabled_) {
            fail("FEATURE_DISABLED", "virtual display management is disabled on the controlled device");
            return;
        }
        if (settings_.IsGameHookMode()) {
            fail("UNSUPPORTED_CAPTURE_MODE", "virtual displays are only available in desktop capture mode");
            return;
        }

        std::optional<VirtualDisplayResponse> cached;
        bool already_pending = false;
        const auto [initial_owned_count, initial_generation] =
            app_->GetVirtualDisplayStatusSnapshot();
        {
            std::scoped_lock lock(virtual_display_->mutex);
            if (const auto it = virtual_display_->completed.find(request_id);
                it != virtual_display_->completed.end()) {
                cached = it->second.response;
            }
            else if (virtual_display_->pending.contains(request_id)) {
                already_pending = true;
            }
            else {
                virtual_display_->pending.emplace(request_id, PendingVirtualDisplayRequest {
                    .device_id = device_id,
                    .stream_id = stream_id,
                    .deadline = std::chrono::steady_clock::now() +
                        VirtualDisplayServiceResponseTimeout(request.operation()),
                    .operation = request.operation(),
                    .initial_owned_display_count = initial_owned_count,
                    .initial_topology_generation = initial_generation,
                    .required_capture_epoch = virtual_display_->capture_epoch + 1,
                });
            }
        }
        if (cached) {
            SendVirtualDisplayResponse(app_, device_id, stream_id, *cached);
            return;
        }
        if (already_pending) {
            fail("REQUEST_IN_PROGRESS", "the same virtual display request is already running");
            return;
        }

        auto coordinator = virtual_display_;
        LOGI("Virtual display request started: request={}, operation={}, owned={}, generation={}",
             request_id, static_cast<int>(request.operation()), initial_owned_count,
             initial_generation);
        app_->RequestVirtualDisplay(
            request_id,
            static_cast<int>(request.operation()),
            request.width(),
            request.height(),
            request.refresh_hz(),
            [coordinator, app](const MsgVirtualDisplayServiceResult& result) {
                // The display-change path rebuilds the Render state lane and
                // can discard work queued at exactly that boundary. Persist
                // the Service result under the coordinator lock immediately;
                // capture notifications and network responses remain safe to
                // arrive in either order.
                {
                    std::scoped_lock lock(coordinator->mutex);
                    const auto it = coordinator->pending.find(result.request_id_);
                    if (it == coordinator->pending.end()) {
                        LOGW("Late virtual display Service result ignored: request={}, accepted={}, code={}",
                             result.request_id_, result.accepted_, result.error_code_);
                        return;
                    }
                    LOGI("Virtual display Service result received: request={}, accepted={}, "
                         "changed={}, owned={}, generation={}, code={}",
                         result.request_id_, result.accepted_, result.topology_changed_,
                         result.owned_display_count_, result.topology_generation_,
                         result.error_code_);
                    it->second.service_result = result;
                    if (result.accepted_ && result.topology_changed_) {
                        // Driver completion and capture recovery are two
                        // different phases. A slow but valid Service reply
                        // must still receive the full first-frame budget.
                        it->second.deadline = std::chrono::steady_clock::now() +
                            kVirtualDisplayCaptureRebuildTimeout;
                    }
                }
                CompleteVirtualDisplayRequests(app, coordinator);
            });
    }

//    void NetworkEventIngress::ProcessFocusOutEvent() {
//        win_event_replayer_->HandleFocusOutEvent();
//    }

//    void NetworkEventIngress::ProcessExitControlledEnd() {
//        LOGI("recv exit controlled end msg, render will exit and restart.");
//        //win_event_replayer_->SimulateCtrlWinShiftB();
//        //exit(0);
//    }

    void NetworkEventIngress::ProcessAck(const std::shared_ptr<PxPluginNetClientEvent>& ev, const std::shared_ptr<Message>& m) {
        auto sub = m->ack();
        auto ack = std::make_shared<NetMessageAck>();
        ack->send_time_ = sub.send_time();
        ack->resp_time_ = sub.resp_time();
        ack->ch_type_ = ev->nt_channel_type_;
        ack->msg_type_ = m->type();
        if (ev->ack_callback_) {
            ev->ack_callback_(ack);
        }
        else if (ev->from_plugin_) {
            ev->from_plugin_->OnMessageAck(ack);
        }
    }
}
