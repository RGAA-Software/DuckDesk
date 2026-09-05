#include "render_event_ingress.h"

#include <chrono>
#include <functional>
#include <type_traits>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "architecture/encoders/video_encoder_module.h"
#include "architecture/services/file_transfer_service.h"
#include "architecture/services/file_transfer_types.h"
#include "architecture/services/voice_call_service.h"
#include "architecture/sources/monitor_capture_source.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "px_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_message_new/rp_proto_converter.h"
#include "px_render/ingress/network_event_ingress.h"
#include "px_render/modules/module_ids.h"
#include "px_render/modules/render_module_registry.h"
#include "px_render_panel_message.pb.h"
#include "rd_app.h"
#include "rd_context.h"
#include "rd_statistics.h"

namespace px {

RenderEventIngress::RenderEventIngress(const std::shared_ptr<RdApplication>& app)
    : app_(app), context_(app->GetContext()), module_registry_(context_->GetRenderModuleRegistry()), network_ingress_(NetworkEventIngress::Make(app)),
      msg_notifier_(context_->GetMessageNotifier()), stat_(RdStatistics::Instance()) {}

void RenderEventIngress::ProcessWebRtcEvent(const std::string& source_id, const WebRtcEvent& event) {
    const auto ingress = std::ref(*this);
    std::visit(
        [ingress, &source_id](const auto& value) {
            auto& owner = ingress.get();
            using Event = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Event, WebRtcNetClientEvent>) {
                auto network_event = std::make_shared<NetworkClientEvent>();
                network_event->is_proto_ = value.is_proto;
                network_event->message_ = value.message;
                network_event->transport_type_ = value.transport_type;
                network_event->channel_type_ = value.channel_type;
                network_event->connection_instance_id_ = value.connection_instance_id;
                owner.network_ingress_->ProcessNetEvent(network_event, source_id);
            } else if constexpr (std::is_same_v<Event, WebRtcClientConnectedEvent>) {
                auto connected = std::make_shared<ClientConnectedEvent>();
                connected->connection_id_ = value.connection_id;
                connected->stream_id_ = value.stream_id;
                connected->connection_type_ = value.connection_type;
                connected->visitor_device_id_ = value.visitor_device_id;
                connected->begin_timestamp_ = value.begin_timestamp;
                owner.network_ingress_->ProcessClientConnectedEvent(connected, source_id);
            } else if constexpr (std::is_same_v<Event, WebRtcClientDisconnectedEvent>) {
                auto disconnected = std::make_shared<ClientDisconnectedEvent>();
                disconnected->logical_session_id_ = value.logical_session_id;
                disconnected->connection_id_ = value.connection_id;
                disconnected->connection_instance_id_ = value.connection_instance_id;
                disconnected->stream_id_ = value.stream_id;
                disconnected->visitor_device_id_ = value.visitor_device_id;
                disconnected->end_timestamp_ = value.end_timestamp;
                disconnected->duration_ = value.duration;
                owner.network_ingress_->ProcessClientDisConnectedEvent(disconnected, source_id);
            } else if constexpr (std::is_same_v<Event, WebRtcFileTransferDisconnectedEvent>) {
                const auto prefix = source_id == kNetWebRtcRemoteLibraryId ? "rtc:" : "rtc-local:";
                const auto binding_id = std::string(prefix) + value.stream_id;
                const auto registry = owner.app_->GetLogicalSessionRegistry();
                const auto logical_session_id =
                    registry ? registry->FindLogicalSessionIdByBinding(binding_id, static_cast<std::int64_t>(TimeUtil::GetCurrentTimestamp()))
                             : std::nullopt;
                if (!logical_session_id) {
                    LOGI("Ignore stale WebRTC FT-only disconnect without a logical binding: stream {}, transport {}", value.stream_id, source_id);
                    return;
                }
                if (const auto service = owner.context_->GetFileTransferService()) {
                    service->HandleRouteDisconnected(FileTransferRouteDisconnected{
                        .logical_session_id = *logical_session_id,
                        .stream_id = value.stream_id,
                        .transport_id = source_id,
                        .connection_id = value.connection_instance_id,
                    });
                }
            } else if constexpr (std::is_same_v<Event, WebRtcAnswerSdpEvent>) {
                owner.SendWebRtcAnswerSdpToRemote(value);
            } else if constexpr (std::is_same_v<Event, WebRtcIceEvent>) {
                owner.SendWebRtcIceToRemote(value);
            } else if constexpr (std::is_same_v<Event, WebRtcVoicePcmEvent>) {
                if (const auto service = owner.context_->GetVoiceCallService(); service && !value.pcm.empty()) {
                    service->HandleWebRtcPcm(value.stream_id, value.call_id, value.pcm, value.sample_rate, value.channels);
                }
            } else if constexpr (std::is_same_v<Event, WebRtcInsertIdrEvent>) {
                owner.module_registry_->InsertIdr(value.monitor_name);
            } else if constexpr (std::is_same_v<Event, WebRtcSelectCaptureMonitorEvent>) {
                const auto capture = owner.app_->GetWorkingMonitorCaptureSource();
                if (capture && !value.monitor_name.empty()) {
                    capture->SelectMonitor(value.monitor_name);
                }
            } else if constexpr (std::is_same_v<Event, WebRtcConfigureEncoderEvent>) {
                const auto weak_self = owner.weak_from_this();
                owner.app_->PostGlobalTask([weak_self, value]() {
                    const auto self = weak_self.lock();
                    if (!self) {
                        return;
                    }
                    std::unordered_set<std::shared_ptr<VideoEncoderModule>> unique_encoders;
                    for (const auto& [monitor_name, encoder] : self->app_->GetWorkingVideoEncoders()) {
                        static_cast<void>(monitor_name);
                        if (encoder) {
                            unique_encoders.insert(encoder);
                        }
                    }
                    for (const auto& encoder : unique_encoders) {
                        encoder->Reconfigure(value.monitor_name, value.bits_per_second, value.frames_per_second);
                    }
                });
            }
        },
        event);
}

void RenderEventIngress::ProcessRenderEvent(const RenderEventEnvelope& envelope) {
    const auto ingress = std::ref(*this);
    std::visit(
        [ingress, &envelope](const auto& event) {
            if (!event) {
                return;
            }
            auto& owner = ingress.get();
            using Event = typename std::decay_t<decltype(event)>::element_type;
            if constexpr (std::is_same_v<Event, NetworkClientEvent>) {
                owner.network_ingress_->ProcessNetEvent(event, envelope.source_id);
            } else if constexpr (std::is_same_v<Event, ClientConnectedEvent>) {
                owner.network_ingress_->ProcessClientConnectedEvent(event, envelope.source_id);
            } else if constexpr (std::is_same_v<Event, ClientDisconnectedEvent>) {
                owner.network_ingress_->ProcessClientDisConnectedEvent(event, envelope.source_id);
            } else if constexpr (std::is_same_v<Event, CaptureMonitorInfoChangedEvent>) {
                owner.network_ingress_->ProcessCapturingMonitorInfoEvent(event);
            } else if constexpr (std::is_same_v<Event, KeyFrameRequestEvent>) {
                owner.module_registry_->InsertIdr(event->monitor_name_);
            } else if constexpr (std::is_same_v<Event, ReferenceFrameInvalidationEvent>) {
                if (!owner.module_registry_->InvalidateReferenceFrame(event->monitor_name_, event->invalid_frame_index_)) {
                    LOGW("RFI not accepted by any encoder, fallback to IDR immediately");
                    owner.module_registry_->InsertIdr(event->monitor_name_);
                }
            } else if constexpr (std::is_same_v<Event, DataSentEvent>) {
                owner.stat_->AppendMediaBytes(static_cast<std::int64_t>(event->size_));
            } else if constexpr (std::is_same_v<Event, PanelStreamMessageEvent>) {
                const auto weak_self = owner.weak_from_this();
                owner.app_->PostGlobalTask([weak_self, event]() {
                    if (const auto self = weak_self.lock()) {
                        self->ProcessPanelStreamMessage(event);
                    }
                });
            } else if constexpr (std::is_same_v<Event, RelayAliveEvent>) {
                owner.ReportRelayAlive(event->device_id_, static_cast<std::int64_t>(envelope.created_timestamp));
            } else if constexpr (std::is_same_v<Event, StreamingParametersRequestedEvent>) {
                owner.app_->HandleForceGdiEvent(event->force_gdi_);
            } else if constexpr (std::is_same_v<Event, RedeemConnectionTicketEvent>) {
                owner.app_->RedeemConnectionTicket(event->ticket_, event->client_nonce_, event->instance_id_, std::move(event->callback_));
            } else if constexpr (std::is_same_v<Event, AdmitLogicalSessionEvent>) {
                if (!event->callback_) {
                    return;
                }
                const auto registry = owner.app_->GetLogicalSessionRegistry();
                if (!registry) {
                    event->callback_(LogicalSessionAdmission{});
                    return;
                }
                const auto now_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const auto admission = registry->Bind(event->grant_, event->transport_, event->binding_id_, event->takeover_, now_ms);
                if (admission.release_previous_controller_input && owner.network_ingress_) {
                    owner.network_ingress_->ReleaseControllerInput(LogicalSessionInputLease{
                        .logical_session_id = admission.previous_controller_session_id,
                        .generation = admission.previous_controller_lease_generation,
                    });
                    const auto previous_stream = registry->FindStreamId(admission.previous_controller_session_id);
                    if (previous_stream) {
                        owner.module_registry_->ApplyLogicalSessionCapabilities(PxLogicalSessionCapabilityUpdate{
                            .stream_id_ = *previous_stream,
                            .permissions_ = {"view", "audio"},
                        });
                    }
                }
                event->callback_(admission);
            } else if constexpr (std::is_same_v<Event, CloseLogicalSessionBindingEvent>) {
                const auto registry = owner.app_->GetLogicalSessionRegistry();
                if (!registry || event->logical_session_id_.empty() || event->binding_id_.empty()) {
                    return;
                }
                const auto now_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const auto closed = registry->CloseBinding(event->logical_session_id_, event->binding_id_, now_ms);
                if (closed.release_controller_input && owner.network_ingress_) {
                    owner.network_ingress_->ReleaseControllerInput(LogicalSessionInputLease{
                        .logical_session_id = closed.logical_session_id,
                        .binding_id = event->binding_id_,
                        .generation = closed.lease_generation,
                    });
                }
            }
        },
        envelope.payload);
}

void RenderEventIngress::SendWebRtcAnswerSdpToRemote(const WebRtcAnswerSdpEvent& event) {
    const auto message = std::make_shared<Message>();
    message->set_type(MessageType::kSigAnswerSdpMessage);
    message->mutable_sig_answer_sdp()->set_sdp(event.sdp);
    module_registry_->SendRelaySignalingMessage(event.stream_id, ProtoAsData(message));
    LOGI("Send WebRTC SDP by relay: {}", event.stream_id);
}

void RenderEventIngress::SendWebRtcIceToRemote(const WebRtcIceEvent& event) {
    const auto message = std::make_shared<Message>();
    message->set_type(MessageType::kSigIceMessage);
    message->mutable_sig_ice()->set_ice(event.ice);
    message->mutable_sig_ice()->set_mid(event.mid);
    message->mutable_sig_ice()->set_sdp_mline_index(event.sdp_mline_index);
    module_registry_->SendRelaySignalingMessage(event.stream_id, ProtoAsData(message));
    LOGI("Send WebRTC ICE by relay: {}", event.ice);
}

void RenderEventIngress::ProcessPanelStreamMessage(const std::shared_ptr<PanelStreamMessageEvent>& event) {
    if (!event || !event->body_) {
        return;
    }
    try {
        LOGI("ProcessPanelStreamMessage: {}", event->body_->AsString());
        const auto object = nlohmann::json::parse(event->body_->AsString());
        const auto event_name = object["event"].get<std::string>();
        const auto from_device = object["from_device"].get<std::string>();
        if (event_name == "restart_render") {
            app_->SendAppMessage(MsgPanelStreamRestartRender{.from_device_ = from_device});
        } else if (event_name == "lock_screen") {
            app_->SendAppMessage(MsgPanelStreamLockScreen{.from_device_ = from_device});
        } else if (event_name == "restart_device") {
            app_->SendAppMessage(MsgPanelStreamRestartDevice{.from_device_ = from_device});
        } else if (event_name == "shutdown_device") {
            app_->SendAppMessage(MsgPanelStreamShutdownDevice{.from_device_ = from_device});
        }
    } catch (const std::exception& error) {
        LOGE("ProcessPanelStreamMessage failed: {}, body: {}", error.what(), event->body_->AsString());
    }
}

void RenderEventIngress::ReportRelayAlive(const std::string& device_id, const std::int64_t timestamp) {
    pxrp::RpMessage message;
    message.set_type(pxrp::kRpRelayAlive);
    auto& relay_alive = *message.mutable_relay_alive();
    relay_alive.set_device_id(device_id);
    relay_alive.set_timestamp(timestamp);
    app_->PostPanelMessage(RpProtoAsData(&message));
}

} // namespace px
