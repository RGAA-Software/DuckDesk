//
// Created by RGAA on 15/11/2024.
//

#include "render_module_registry.h"
#include <algorithm>
#include <mutex>
#include <type_traits>
#include "rd_app.h"
#include "px_render/modules/module_ids.h"
#include "rd_context.h"
#include "px_common/log.h"
#include "px_common/win32/win_helper.h"
#include "px_common/string_util.h"
#include "px_render/ingress/render_event_ingress.h"
#include "px_render/pipeline/encoded_video_fanout.h"
#include "settings/rd_settings.h"
#include "px_common/folder_util.h"
#include "px_capture/capture_message.h"
#include "architecture/modules/render_module.h"
#include "architecture/encoders/video_encoder_module.h"
#include "architecture/sources/monitor_capture_source.h"
#include "px_render/network/transport_types.h"
#include "architecture/services/file_transfer_service.h"
#include "architecture/services/voice_call_service.h"
#include "px_render/architecture/sources/dda/dda_capture_source.h"
#include "px_render/architecture/sources/gdi/gdi_capture_source.h"
#include "px_render/architecture/encoders/ffmpeg/ffmpeg_video_encoder.h"
#include "px_render/architecture/encoders/amf/amf_video_encoder.h"
#include "px_render/architecture/encoders/nvenc/nvenc_encoder_module.h"
#include "px_render/network/ws/ws_transport.h"
#include "px_render/network/udp/udp_transport.h"
#include "px_render/network/relay/relay_transport.h"
#include "network/webrtc_transport_host.h"

namespace px {
namespace {
WebRtcEncodedVideoType ToWebRtcEncodedVideoType(const EncodedVideoType type) {
    switch (type) {
    case EncodedVideoType::kH264:
        return WebRtcEncodedVideoType::kH264;
    case EncodedVideoType::kH265:
        return WebRtcEncodedVideoType::kH265;
    case EncodedVideoType::kVp8:
        return WebRtcEncodedVideoType::kVp8;
    case EncodedVideoType::kVp9:
        return WebRtcEncodedVideoType::kVp9;
    case EncodedVideoType::kAv1:
        return WebRtcEncodedVideoType::kAv1;
    }
    return WebRtcEncodedVideoType::kH264;
}

WebRtcTransportSettings MakeWebRtcSettings(const RenderRuntimeSettings& info) {
    return WebRtcTransportSettings{
        .device_id = info.device_id,
        .device_random_password = info.device_random_password,
        .device_safety_password = info.device_safety_password,
        .relay_host = info.relay_host,
        .relay_port = info.relay_port,
        .can_be_operated = info.can_be_operated,
        .direct_allow_takeover = info.direct_allow_takeover,
        .relay_enabled = info.relay_enabled,
        .language = info.language,
        .file_transfer_enabled = info.file_transfer_enabled,
        .audio_enabled = info.audio_enabled,
        .appkey = info.appkey,
        .max_transmit_speed = info.max_transmit_speed,
        .max_receive_speed = info.max_receive_speed,
        .role = info.role,
    };
}

} // namespace

std::shared_ptr<RenderModuleRegistry> RenderModuleRegistry::Make(const std::shared_ptr<RdApplication>& app) {
    return std::make_shared<RenderModuleRegistry>(app);
}

RenderModuleRegistry::RenderModuleRegistry(const std::shared_ptr<RdApplication>& app) : settings_(*RdSettings::Instance()) {
    this->app_ = app;
    this->context_ = app->GetContext();
}

RenderModuleRegistry::~RenderModuleRegistry() {
    exiting_ = true;
}

void RenderModuleRegistry::StartModules() {
    exiting_ = false;
    auto base_path = WinHelper::GetExeFolderPath();
    auto base_data_path = FolderUtil::GetProgramDataPath();
    LOGI("module base path: {}", base_path);
    LOGI("module base data path: {}", StringUtil::ToUTF8(base_data_path));

    const RenderModuleConfiguration base_configuration{
        .async_runtime = context_->GetAsyncRuntime(),
        .base_path = base_path,
        .base_data_path = base_data_path,
        .capture_audio_device_id = {},
        .ws_listen_port = settings_.transmission_.listening_port_,
        .udp_listen_port = settings_.transmission_.listening_port_,
        .device_id = settings_.device_id_,
        .direct_allow_takeover = settings_.direct_allow_takeover_,
        .relay_device_id = settings_.relay_device_id_,
        .relay_enabled = settings_.relay_enabled_,
        .relay_host = settings_.relay_host_,
        .relay_port = settings_.relay_port_,
        .language = settings_.language_,
        .appkey = settings_.appkey_,
        .app_mode = settings_.IsGameHookMode() ? "game-hook" : (settings_.IsWebViewMode() ? "webview" : "desktop"),
    };
    const auto register_builtin = [base_configuration](const std::shared_ptr<RenderModule>& module, const std::string& module_name) -> bool {
        auto configuration = base_configuration;
        configuration.instance_name = module_name;
        if (!module || module->Id().empty() || !module->Start(configuration)) {
            LOGE("event=module.start component=render_module_registry module={} "
                 "delivery=static outcome=failed",
                 module_name);
            return false;
        }
        LOGI("event=module.start component=render_module_registry module={} "
             "delivery=static outcome=success",
             module_name);
        return true;
    };
    const auto dda_capture = std::make_shared<DdaCaptureSource>();
    const auto weak_registry = weak_from_this();
    dda_capture->ConfigureMediaBacklogProbe([weak_registry]() {
        const auto registry = weak_registry.lock();
        return registry ? registry->QueuedNetworkMediaMessages() : std::int64_t{0};
    });
    if (register_builtin(dda_capture, "cap_dda")) {
        dda_capture_ = dda_capture;
    }
    const auto gdi_capture = std::make_shared<GdiCaptureSource>();
    if (register_builtin(gdi_capture, "cap_gdi")) {
        gdi_capture_ = gdi_capture;
    }
    const auto ffmpeg_encoder = std::make_shared<FfmpegVideoEncoder>();
    if (register_builtin(ffmpeg_encoder, "enc_ffmpeg")) {
        ffmpeg_encoder_ = ffmpeg_encoder;
    }
    const auto amf_encoder = std::make_shared<AmfVideoEncoder>();
    if (register_builtin(amf_encoder, "enc_amf")) {
        amf_encoder_ = amf_encoder;
    }
    const auto nvenc_encoder = std::make_shared<NvencEncoderModule>();
    if (register_builtin(nvenc_encoder, "enc_nvenc")) {
        nvenc_encoder_ = nvenc_encoder;
    }
    const auto ws_transport = std::make_shared<WsTransport>(context_->GetAsyncRuntime());
    if (register_builtin(ws_transport, "net_ws")) {
        ws_transport_ = ws_transport;
    }
    const auto udp_transport = std::make_shared<UdpTransport>(context_->GetAsyncRuntime());
    if (register_builtin(udp_transport, "net_udp")) {
        udp_transport_ = udp_transport;
    }
    const auto relay_transport = std::make_shared<RelayTransport>(context_->GetAsyncRuntime());
    if (register_builtin(relay_transport, "net_relay")) {
        relay_transport_ = relay_transport;
    }

    webrtc_transport_host_ = WebRtcTransportHost::Create();
    const WebRtcTransportConfiguration webrtc_configuration{
        .async_runtime = context_->GetAsyncRuntime(),
        .base_path = base_path,
        .base_data_path = base_data_path,
        .capture_audio_device_id = {},
        .device_id = settings_.device_id_,
        .direct_allow_takeover = settings_.direct_allow_takeover_,
        .relay_enabled = settings_.relay_enabled_,
        .language = settings_.language_,
        .appkey = settings_.appkey_,
    };
    for (const auto& transport : webrtc_transport_host_->CreateTransports()) {
        if (!transport) {
            continue;
        }
        if (!transport->Start(webrtc_configuration)) {
            LOGE("event=webrtc.library.start component=render_module_registry "
                 "library={} outcome=failed recoverable=false",
                 transport->BaseName());
            continue;
        }
        if (transport->Kind() == WebRtcTransportKind::kRemote) {
            rtc_transport_ = transport;
        } else {
            rtc_local_transport_ = transport;
        }
        const auto info = transport->Info();
        LOGI("event=webrtc.library.start component=render_module_registry "
             "library={} id={} version={} outcome=success",
             transport->BaseName(), info.id, info.version_name);
    }

    WsTransport::LocalRtcAllocator local_rtc_allocator;
    if (rtc_local_transport_) {
        local_rtc_allocator = [weak_registry](const std::shared_ptr<PxLocalRtcRequestInfo>& request, WsTransport::LocalRtcCompletion completion) {
            if (const auto registry = weak_registry.lock()) {
                return registry->AllocateRtcLocalInstance(request, std::move(completion));
            }
            return PxLocalRtcAllocResult::kFailed;
        };
    }
    WsTransport::UdpAssociationUpdater udp_association_updater;
    if (udp_transport_) {
        udp_association_updater = [weak_registry](const UdpMediaAssociation& association) {
            if (const auto registry = weak_registry.lock()) {
                return registry->UpdateUdpMediaAssociation(association);
            }
            return false;
        };
    }

    ws_transport->ConfigureNetworkServices(
        [weak_registry](const std::shared_ptr<Data>& message, const bool run_through) {
            if (const auto registry = weak_registry.lock()) {
                registry->BroadcastNetworkMessage(message, run_through);
            }
        },
        [weak_registry](const std::string& stream_id, const std::shared_ptr<Data>& message, const bool run_through) {
            if (const auto registry = weak_registry.lock()) {
                registry->BroadcastFileTransferMessage(stream_id, message, run_through);
            }
        },
        std::move(local_rtc_allocator), std::move(udp_association_updater));
    const std::weak_ptr<RdApplication> weak_application = app_;
    ws_transport->ConfigureIpcMediaIngress(
        [weak_application](const CaptureVideoFrame& frame) {
            if (const auto application = weak_application.lock()) {
                application->OnCapturedVideoFrame(frame);
            }
        },
        [weak_application](const CaptureAudioFrame& frame) {
            if (const auto application = weak_application.lock()) {
                application->OnIpcAudioFrame(frame);
            }
        });
}

void RenderModuleRegistry::BindIngressCallbacks() {
    {
        std::lock_guard lock(routing_mtx_);
        control_ingress_ = std::make_shared<RenderEventIngress>(app_);
        encoded_video_fanout_ = EncodedVideoFanout::Make(app_);
    }
    auto weak_self = weak_from_this();
    VisitAllModules([&](const std::shared_ptr<RenderModule>& module) {
        if (module->Kind() != RenderModuleKind::kNetwork) {
            return;
        }
        module->SetEventCallback([weak_self](const RenderEventEnvelope& event) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            std::shared_ptr<RenderEventIngress> router;
            {
                std::lock_guard lock(self->routing_mtx_);
                router = self->control_ingress_;
            }
            if (router) {
                router->ProcessRenderEvent(event);
            }
        });
    });
    VisitWebRtcLibraries([weak_self](const std::shared_ptr<WebRtcTransportHandle>& library) {
        const auto source_id = library->Info().id;
        library->SetEventCallback([weak_self, source_id](const WebRtcEvent& event) {
            const auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            std::shared_ptr<RenderEventIngress> router;
            {
                std::lock_guard lock(self->routing_mtx_);
                router = self->control_ingress_;
            }
            if (router) {
                router->ProcessWebRtcEvent(source_id, event);
            }
        });
    });
    for (const auto& capture : {GetDdaCapture(), GetGdiCapture()}) {
        if (!capture) {
            continue;
        }
        capture->SetEventCallback([weak_self](const RenderEventEnvelope& envelope) {
            const auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            std::shared_ptr<RdApplication> app;
            std::shared_ptr<RenderEventIngress> router;
            {
                std::scoped_lock lock(self->routing_mtx_);
                app = self->app_;
                router = self->control_ingress_;
            }
            if (!app) {
                return;
            }
            std::visit(
                [&app, &router, &envelope](const auto& event) {
                    using Event = typename std::decay_t<decltype(event)>::element_type;
                    if (!event) {
                        return;
                    }
                    if constexpr (std::is_same_v<Event, CapturedVideoFrameEvent>) {
                        app->OnCapturedVideoFrame(event->frame_);
                    } else if constexpr (std::is_same_v<Event, CursorUpdatedEvent>) {
                        app->OnCapturedCursorBitmap(event->cursor_info_);
                    } else if (router) {
                        router->ProcessRenderEvent(envelope);
                    }
                },
                envelope.payload);
        });
    }
    VisitEncoders([weak_self](const std::shared_ptr<VideoEncoderModule>& encoder) {
        encoder->SetEventCallback([weak_self](const RenderEventEnvelope& envelope) {
            const auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            std::shared_ptr<EncodedVideoFanout> fanout;
            {
                std::scoped_lock lock(self->routing_mtx_);
                fanout = self->encoded_video_fanout_;
            }
            std::visit(
                [&fanout](const auto& event) {
                    using Event = typename std::decay_t<decltype(event)>::element_type;
                    if constexpr (std::is_same_v<Event, EncodedVideoFrameEvent>) {
                        if (event && fanout) {
                            fanout->ProcessEncodedVideoFrameEvent(event);
                        }
                    }
                },
                envelope.payload);
        });
    });
}

void RenderModuleRegistry::StopRouting() {
    if (exiting_.exchange(true)) {
        return;
    }
    VisitAllModules([](const std::shared_ptr<RenderModule>& module) { module->SetEventCallback({}); });
    VisitWebRtcLibraries([](const std::shared_ptr<WebRtcTransportHandle>& library) { library->SetEventCallback({}); });
    {
        std::lock_guard lock(routing_mtx_);
        control_ingress_.reset();
        encoded_video_fanout_.reset();
        app_.reset();
    }
}

PxAwaitable<PxResult<void>> RenderModuleRegistry::StopNetworkIngressAsync(const std::chrono::steady_clock::time_point deadline) {
    std::shared_ptr<WsTransport> ws_transport;
    std::shared_ptr<UdpTransport> udp_transport;
    {
        std::shared_lock lock(modules_mtx_);
        ws_transport = ws_transport_;
        udp_transport = udp_transport_;
    }
    if (ws_transport) {
        const auto stopped = co_await WsTransport::StopAsync(ws_transport, deadline);
        if (!stopped) {
            co_return stopped;
        }
    }
    if (udp_transport) {
        co_return co_await UdpTransport::StopAsync(udp_transport, deadline);
    }
    co_return PxResult<void>::Success();
}

PxAwaitable<PxResult<void>> RenderModuleRegistry::StopWebRtcLibrariesAsync(const std::chrono::steady_clock::time_point deadline) {
    std::vector<std::shared_ptr<WebRtcTransportHandle>> libraries;
    {
        std::shared_lock lock(modules_mtx_);
        if (rtc_transport_) {
            libraries.push_back(rtc_transport_);
        }
        if (rtc_local_transport_) {
            libraries.push_back(rtc_local_transport_);
        }
    }
    for (const auto& library : libraries) {
        const auto stopped = co_await WebRtcTransportHandle::StopAsync(library, deadline);
        if (!stopped) {
            co_return stopped;
        }
    }
    co_return PxResult<void>::Success();
}

void RenderModuleRegistry::StopModules() {
    // reject new visitors first, the event callback registered in
    // BindIngressCallbacks also checks this flag before routing
    exiting_ = true;
    // Wait until in-flight visitors leave, then detach the explicit owners;
    // Stop/Destroy run outside the lock because modules may fire
    // events which re-enter the visiting functions
    std::vector<std::shared_ptr<RenderModule>> modules;
    std::vector<std::shared_ptr<WebRtcTransportHandle>> webrtc_libraries;
    {
        std::unique_lock<std::shared_mutex> lock(modules_mtx_);
        if (ffmpeg_encoder_)
            modules.push_back(ffmpeg_encoder_);
        if (nvenc_encoder_)
            modules.push_back(nvenc_encoder_);
        if (amf_encoder_)
            modules.push_back(amf_encoder_);
        if (dda_capture_)
            modules.push_back(dda_capture_);
        if (gdi_capture_)
            modules.push_back(gdi_capture_);
        if (ws_transport_)
            modules.push_back(ws_transport_);
        if (udp_transport_)
            modules.push_back(udp_transport_);
        if (relay_transport_)
            modules.push_back(relay_transport_);
        ffmpeg_encoder_.reset();
        nvenc_encoder_.reset();
        amf_encoder_.reset();
        dda_capture_.reset();
        gdi_capture_.reset();
        ws_transport_.reset();
        udp_transport_.reset();
        relay_transport_.reset();
        if (rtc_transport_) {
            webrtc_libraries.push_back(std::move(rtc_transport_));
        }
        if (rtc_local_transport_) {
            webrtc_libraries.push_back(std::move(rtc_local_transport_));
        }
    }
    for (const auto& module : modules) {
        module->Stop();
    }
    for (const auto& module : modules) {
        module->Destroy();
    }
    for (const auto& library : webrtc_libraries) {
        library->Stop();
    }
    for (const auto& library : webrtc_libraries) {
        library->Destroy();
    }
    webrtc_libraries.clear();
    if (webrtc_transport_host_) {
        webrtc_transport_host_->Reset();
        webrtc_transport_host_.reset();
    }
    control_ingress_.reset();
    encoded_video_fanout_.reset();
}

std::shared_ptr<VideoEncoderModule> RenderModuleRegistry::GetFFmpegEncoder() {
    std::shared_lock lock(modules_mtx_);
    return ffmpeg_encoder_;
}

std::shared_ptr<VideoEncoderModule> RenderModuleRegistry::GetNvencEncoder() {
    std::shared_lock lock(modules_mtx_);
    return nvenc_encoder_;
}

std::shared_ptr<VideoEncoderModule> RenderModuleRegistry::GetAmfEncoder() {
    std::shared_lock lock(modules_mtx_);
    return amf_encoder_;
}

std::shared_ptr<MonitorCaptureSource> RenderModuleRegistry::GetDdaCapture() {
    std::shared_lock lock(modules_mtx_);
    return dda_capture_;
}

std::shared_ptr<MonitorCaptureSource> RenderModuleRegistry::GetGdiCapture() {
    std::shared_lock lock(modules_mtx_);
    return gdi_capture_;
}

void RenderModuleRegistry::SyncUdpInfo(const std::int64_t socket_fd, const std::string& device_id, const std::string& stream_id) {
    // UDP media association is now value-driven through
    // UpdateUdpMediaAssociation; the former generic transport sync state
    // had no UDP consumer.
    static_cast<void>(socket_fd);
    static_cast<void>(device_id);
    static_cast<void>(stream_id);
}

bool RenderModuleRegistry::IsRelayConnected() {
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        relay = relay_transport_;
    }
    return relay && relay->IsWorking();
}

void RenderModuleRegistry::SubmitRtcLocalSharedTexture(const std::string& monitor_name, const std::uint64_t frame_index, const int frame_width,
                                                       const int frame_height, const std::uint64_t shared_handle, const std::int64_t adapter_id,
                                                       const std::uint64_t frame_format) {
    std::shared_ptr<WebRtcTransportHandle> rtc_local;
    {
        std::shared_lock lock(modules_mtx_);
        rtc_local = rtc_local_transport_;
    }
    if (rtc_local) {
        rtc_local->SubmitLocalSharedTexture(monitor_name, frame_index, frame_width, frame_height, shared_handle, adapter_id, frame_format);
    }
}

void RenderModuleRegistry::SubmitRtcLocalYuv(const std::string& monitor_name, const std::uint64_t frame_index, const int frame_width,
                                             const int frame_height, const std::shared_ptr<Image>& image) {
    std::shared_ptr<WebRtcTransportHandle> rtc_local;
    {
        std::shared_lock lock(modules_mtx_);
        rtc_local = rtc_local_transport_;
    }
    if (rtc_local) {
        rtc_local->SubmitLocalYuv(monitor_name, frame_index, frame_width, frame_height, image);
    }
}

void RenderModuleRegistry::UpdateRtcLocalCaptureMonitorInfo(const CaptureMonitorInfoMessage& message) {
    std::shared_ptr<WebRtcTransportHandle> rtc_local;
    {
        std::shared_lock lock(modules_mtx_);
        rtc_local = rtc_local_transport_;
    }
    if (rtc_local) {
        rtc_local->UpdateCaptureMonitorInfo(message);
    }
}

void RenderModuleRegistry::ApplyRtcLocalRemoteSdp(const MsgRtcRemoteSdp& message) {
    std::shared_ptr<WebRtcTransportHandle> rtc_local;
    {
        std::shared_lock lock(modules_mtx_);
        rtc_local = rtc_local_transport_;
    }
    if (rtc_local) {
        rtc_local->ApplyRemoteSdp(message);
    }
}

void RenderModuleRegistry::ApplyRtcLocalRemoteIce(const MsgRtcRemoteIce& message) {
    std::shared_ptr<WebRtcTransportHandle> rtc_local;
    {
        std::shared_lock lock(modules_mtx_);
        rtc_local = rtc_local_transport_;
    }
    if (rtc_local) {
        rtc_local->ApplyRemoteIce(message);
    }
}

PxLocalRtcAllocResult RenderModuleRegistry::AllocateRtcLocalInstance(const std::shared_ptr<PxLocalRtcRequestInfo>& request,
                                                                     std::function<void(const std::shared_ptr<PxLocalRtcReplyInfo>&)>&& completion) {
    std::shared_ptr<WebRtcTransportHandle> rtc_local;
    {
        std::shared_lock lock(modules_mtx_);
        rtc_local = rtc_local_transport_;
    }
    return rtc_local ? rtc_local->AllocateLocalInstance(request, std::move(completion)) : PxLocalRtcAllocResult::kFailed;
}

bool RenderModuleRegistry::UpdateUdpMediaAssociation(const UdpMediaAssociation& association) {
    std::shared_ptr<UdpTransport> udp;
    {
        std::shared_lock lock(modules_mtx_);
        udp = udp_transport_;
    }
    if (udp) {
        udp->UpdateUdpMediaAssociation(association);
        return true;
    }
    return false;
}

void RenderModuleRegistry::BroadcastNetworkMessage(const std::shared_ptr<Data>& message, const bool run_through) {
    if (!message) {
        return;
    }
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<UdpTransport> udp;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        udp = udp_transport_;
        relay = relay_transport_;
    }
    if (ws)
        ws->Broadcast(message, run_through);
    if (udp)
        udp->Broadcast(message, run_through);
    if (relay)
        relay->Broadcast(message, run_through);
    VisitWebRtcLibraries([message, run_through](const std::shared_ptr<WebRtcTransportHandle>& library) { library->Send(message, run_through); });
}

void RenderModuleRegistry::BroadcastTargetStreamMessage(const std::string& stream_id, const std::shared_ptr<Data>& message, const bool run_through) {
    if (!message) {
        return;
    }
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<UdpTransport> udp;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        udp = udp_transport_;
        relay = relay_transport_;
    }
    if (ws)
        static_cast<void>(ws->SendToStream(stream_id, message, run_through));
    if (udp)
        static_cast<void>(udp->SendToStream(stream_id, message, run_through));
    if (relay)
        static_cast<void>(relay->SendToStream(stream_id, message, run_through));
    VisitWebRtcLibraries([&stream_id, &message, run_through](const std::shared_ptr<WebRtcTransportHandle>& library) {
        static_cast<void>(library->SendToStream(stream_id, message, run_through));
    });
}

void RenderModuleRegistry::BroadcastFileTransferMessage(const std::string& stream_id, const std::shared_ptr<Data>& message, const bool run_through) {
    if (!message) {
        return;
    }
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        relay = relay_transport_;
    }
    if (ws)
        static_cast<void>(ws->SendFileTransfer(stream_id, message, run_through));
    if (relay)
        static_cast<void>(relay->SendFileTransfer(stream_id, message, run_through));
    VisitWebRtcLibraries([&stream_id, &message, run_through](const std::shared_ptr<WebRtcTransportHandle>& library) {
        static_cast<void>(library->SendFileTransfer(stream_id, message, run_through));
    });
}

void RenderModuleRegistry::BroadcastRawAudio(const std::shared_ptr<Data>& data, const int samples, const int channels, const int bits) {
    if (!data) {
        return;
    }
    VisitWebRtcLibraries([&data, samples, channels, bits](const std::shared_ptr<WebRtcTransportHandle>& library) {
        library->SubmitRawAudio(data, samples, channels, bits);
    });
}

void RenderModuleRegistry::PublishEncodedVideoMetadata(const std::string& monitor_name, const std::shared_ptr<EncodedVideoFrameEvent>& event) {
    if (!event || !event->data_) {
        return;
    }
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<UdpTransport> udp;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        udp = udp_transport_;
    }
    if (ws) {
        ws->SubmitEncodedVideo(monitor_name, event->type_, event->data_, event->frame_index_, static_cast<int>(event->frame_width_),
                               static_cast<int>(event->frame_height_), event->key_frame_);
    }
    if (udp) {
        udp->SubmitEncodedVideo(monitor_name, event->type_, event->data_, event->frame_index_, static_cast<int>(event->frame_width_),
                                static_cast<int>(event->frame_height_), event->key_frame_);
    }
    VisitWebRtcLibraries([&monitor_name, &event](const std::shared_ptr<WebRtcTransportHandle>& library) {
        library->SubmitEncodedVideo(monitor_name, ToWebRtcEncodedVideoType(event->type_), event->data_, event->frame_index_,
                                    static_cast<int>(event->frame_width_), static_cast<int>(event->frame_height_), event->key_frame_);
    });
}

void RenderModuleRegistry::DispatchNetworkAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
    if (!event) {
        return;
    }
    std::shared_ptr<WsTransport> ws;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
    }
    if (ws)
        ws->HandleAppEvent(event);
}

void RenderModuleRegistry::ApplyLogicalSessionCapabilities(const PxLogicalSessionCapabilityUpdate& update) {
    std::shared_ptr<WsTransport> ws;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
    }
    if (ws)
        ws->ApplyLogicalSessionCapabilities(update);
    VisitWebRtcLibraries([&update](const std::shared_ptr<WebRtcTransportHandle>& library) { library->ApplyLogicalSessionCapabilities(update); });
}

bool RenderModuleRegistry::PostRtcLocalMessage(const std::shared_ptr<Data>& message, const bool run_through) {
    std::shared_ptr<WebRtcTransportHandle> rtc_local;
    {
        std::shared_lock lock(modules_mtx_);
        rtc_local = rtc_local_transport_;
    }
    if (rtc_local && message && rtc_local->ConnectedClientCount() > 0) {
        rtc_local->Send(message, run_through);
        return true;
    }
    return false;
}

void RenderModuleRegistry::SendRelaySignalingMessage(const std::string& stream_id, const std::shared_ptr<Data>& message) {
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        relay = relay_transport_;
    }
    if (!relay || !message) {
        return;
    }
    if (stream_id.empty()) {
        relay->Broadcast(message, true);
        return;
    }
    static_cast<void>(relay->SendToStream(stream_id, message, true));
}

void RenderModuleRegistry::PostWsIpcBinaryMessage(const std::shared_ptr<Data>& message) {
    std::shared_ptr<WsTransport> ws;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
    }
    if (ws && message) {
        ws->SendIpc(message);
    }
}

void RenderModuleRegistry::RegisterWsIpcPid(const std::uint32_t pid) {
    std::shared_ptr<WsTransport> ws;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
    }
    if (ws) {
        ws->RegisterIpcPid(pid);
    }
}

void RenderModuleRegistry::PostWsUserProxyMessage(const std::shared_ptr<Data>& message) {
    std::shared_ptr<WsTransport> ws;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
    }
    if (ws && message) {
        ws->SendUserProxy(message);
    }
}

bool RenderModuleRegistry::IsWsUserProxyConnected() {
    std::shared_ptr<WsTransport> ws;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
    }
    return ws && ws->IsUserProxyConnected();
}

bool RenderModuleRegistry::HasWorkingVideoClient() {
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<UdpTransport> udp;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        udp = udp_transport_;
        relay = relay_transport_;
    }
    if (ws && ws->IsWorking() && !ws->HasOnlyAudioClients())
        return true;
    if (udp && udp->IsWorking() && !udp->HasOnlyAudioClients())
        return true;
    if (relay && relay->IsWorking() && !relay->HasOnlyAudioClients())
        return true;
    for (const auto& library : SnapshotWebRtcLibraries()) {
        if (library->HasVideoClient()) {
            return true;
        }
    }
    return false;
}

std::vector<RenderModuleInfo> RenderModuleRegistry::SnapshotModuleInfo() {
    std::vector<RenderModuleInfo> result;
    const auto modules = SnapshotModules();
    const auto webrtc_libraries = SnapshotWebRtcLibraries();
    result.reserve(modules.size() + webrtc_libraries.size());
    for (const auto& module : modules) {
        result.push_back(RenderModuleInfo{
            .id = module->Id(),
            .name = module->Name(),
            .author = module->Author(),
            .description = module->Description(),
            .version_name = module->VersionName(),
            .version_code = module->VersionCode(),
            .enabled = module->IsEnabled(),
        });
    }
    for (const auto& library : webrtc_libraries) {
        const auto info = library->Info();
        result.push_back(RenderModuleInfo{
            .id = info.id,
            .name = info.name,
            .author = info.author,
            .description = info.description,
            .version_name = info.version_name,
            .version_code = info.version_code,
            .enabled = info.enabled,
        });
    }
    return result;
}

bool RenderModuleRegistry::SetModuleEnabled(const std::string& module_id, const bool enabled) {
    for (const auto& module : SnapshotModules()) {
        if (module->Id() != module_id) {
            continue;
        }
        LOGI("event=module.enable component=render_module_registry "
             "module={} enabled={}",
             module->Name(), enabled);
        module->SetEnabled(enabled);
        return true;
    }
    for (const auto& library : SnapshotWebRtcLibraries()) {
        const auto info = library->Info();
        if (info.id != module_id) {
            continue;
        }
        LOGI("event=module.enable component=render_module_registry "
             "module={} delivery=dynamic-network-library enabled={}",
             info.name, enabled);
        library->SetEnabled(enabled);
        return true;
    }
    return false;
}

void RenderModuleRegistry::DispatchAppEventToModules(const std::shared_ptr<AppBaseEvent>& event) {
    if (!event) {
        return;
    }
    VisitAllModules([&event](const std::shared_ptr<RenderModule>& module) { module->HandleAppEvent(event); });
}

void RenderModuleRegistry::UpdateModuleD3DResources(const std::uint64_t adapter_uid, const Microsoft::WRL::ComPtr<ID3D11Device>& device,
                                                    const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context) {
    VisitAllModules([adapter_uid, device, context](const std::shared_ptr<RenderModule>& module) {
        module->d3d11_devices_[adapter_uid] = device;
        module->d3d11_device_contexts_[adapter_uid] = context;
    });
}

void RenderModuleRegistry::ClearModuleD3DResources(const std::uint64_t adapter_uid) {
    VisitAllModules([adapter_uid](const std::shared_ptr<RenderModule>& module) {
        module->d3d11_devices_.erase(adapter_uid);
        module->d3d11_device_contexts_.erase(adapter_uid);
    });
}

void RenderModuleRegistry::InsertIdr(const std::string& monitor_name) {
    VisitEncoders([&monitor_name](const std::shared_ptr<VideoEncoderModule>& encoder) { encoder->RequestKeyFrame(monitor_name); });
}

bool RenderModuleRegistry::InvalidateReferenceFrame(const std::string& monitor_name, const std::uint64_t invalid_frame_index) {
    bool accepted = false;
    VisitEncoders([&accepted, &monitor_name, invalid_frame_index](const std::shared_ptr<VideoEncoderModule>& encoder) {
        accepted = encoder->InvalidateReferenceFrame(monitor_name, invalid_frame_index) || accepted;
    });
    return accepted;
}

std::vector<std::shared_ptr<RenderModule>> RenderModuleRegistry::SnapshotModules() {
    std::vector<std::shared_ptr<RenderModule>> modules;
    std::shared_lock lock(modules_mtx_);
    if (ffmpeg_encoder_)
        modules.push_back(ffmpeg_encoder_);
    if (nvenc_encoder_)
        modules.push_back(nvenc_encoder_);
    if (amf_encoder_)
        modules.push_back(amf_encoder_);
    if (dda_capture_)
        modules.push_back(dda_capture_);
    if (gdi_capture_)
        modules.push_back(gdi_capture_);
    if (ws_transport_)
        modules.push_back(ws_transport_);
    if (udp_transport_)
        modules.push_back(udp_transport_);
    if (relay_transport_)
        modules.push_back(relay_transport_);
    return modules;
}

std::vector<std::shared_ptr<VideoEncoderModule>> RenderModuleRegistry::SnapshotEncoders() {
    std::vector<std::shared_ptr<VideoEncoderModule>> encoders;
    std::shared_lock lock(modules_mtx_);
    if (ffmpeg_encoder_)
        encoders.push_back(ffmpeg_encoder_);
    if (nvenc_encoder_)
        encoders.push_back(nvenc_encoder_);
    if (amf_encoder_)
        encoders.push_back(amf_encoder_);
    return encoders;
}

std::vector<std::shared_ptr<WebRtcTransportHandle>> RenderModuleRegistry::SnapshotWebRtcLibraries() {
    std::vector<std::shared_ptr<WebRtcTransportHandle>> libraries;
    std::shared_lock lock(modules_mtx_);
    if (rtc_transport_) {
        libraries.push_back(rtc_transport_);
    }
    if (rtc_local_transport_) {
        libraries.push_back(rtc_local_transport_);
    }
    return libraries;
}

void RenderModuleRegistry::VisitAllModules(const std::function<void(const std::shared_ptr<RenderModule>&)>& visitor) {
    for (const auto& module : SnapshotModules()) {
        if (visitor) {
            visitor(module);
        }
    }
}

void RenderModuleRegistry::VisitEncoders(const std::function<void(const std::shared_ptr<VideoEncoderModule>&)>& visitor) {
    for (const auto& encoder : SnapshotEncoders()) {
        if (visitor) {
            visitor(encoder);
        }
    }
}

void RenderModuleRegistry::On1Second() {
    if (exiting_ || !context_) {
        return;
    }
    auto context = context_;
    auto weak_self = weak_from_this();
    context->PostTask([weak_self]() {
        auto self = weak_self.lock();
        if (!self || self->exiting_ || !self->context_ || !self->control_ingress_) {
            return;
        }

        if (const auto voice_call = self->context_->GetVoiceCallService()) {
            voice_call->On1Second();
        }

        // Shared ownership snapshots keep components alive without
        // holding the registry lock while their callbacks execute.
        const auto modules_snapshot = self->SnapshotModules();
        const auto webrtc_snapshot = self->SnapshotWebRtcLibraries();
        if (modules_snapshot.empty() && webrtc_snapshot.empty()) {
            return;
        }

        int media_consumer_count = 0;
        std::shared_ptr<WsTransport> ws;
        std::shared_ptr<UdpTransport> udp;
        std::shared_ptr<RelayTransport> relay;
        {
            std::shared_lock lock(self->modules_mtx_);
            ws = self->ws_transport_;
            udp = self->udp_transport_;
            relay = self->relay_transport_;
        }
        if (ws)
            media_consumer_count += ws->ConnectedClientCount();
        if (udp)
            media_consumer_count += udp->ConnectedClientCount();
        if (relay)
            media_consumer_count += relay->ConnectedClientCount();
        for (const auto& library : webrtc_snapshot) {
            media_consumer_count += library->Kind() == WebRtcTransportKind::kLocal ? library->MediaConsumerCount() : library->ConnectedClientCount();
        }

        // LOGI("connected_client_count: {}", connected_client_count);
        for (const auto& module : modules_snapshot) {
            if (self->exiting_) {
                return;
            }
            module->Tick1Second();

            // connected clients count
            {
                auto event = std::make_shared<MsgConnectedClientCount>();
                // This event drives capture/encoder idle state. It counts
                // hidden media observers too, while public statistics use
                // GetTotalConnectedClientsCount below.
                event->connected_client_count_ = media_consumer_count;
                module->HandleAppEvent(event);
            }
        }
    });
}

void RenderModuleRegistry::DumpModuleInfo() {
    const auto module_info = SnapshotModuleInfo();
    LOGI("====> Total modules: {}", module_info.size());
    int index = 1;
    for (const auto& module : module_info) {
        LOGI("Module {}. [{}] vn: [{}], vc: [{}], enabled: [{}]", index++, module.name, module.version_name, module.version_code, module.enabled);
    }
}

void RenderModuleRegistry::SyncModuleSettings(const RenderRuntimeSettings& info) {
    if (exiting_) {
        return;
    }
    VisitAllModules([&](const std::shared_ptr<RenderModule>& module) { module->UpdateSettings(info); });
    const auto webrtc_settings = MakeWebRtcSettings(info);
    VisitWebRtcLibraries([&webrtc_settings](const std::shared_ptr<WebRtcTransportHandle>& library) { library->UpdateSettings(webrtc_settings); });
    if (const auto service = context_->GetFileTransferService()) {
        static_cast<void>(service->SetEnabled(info.file_transfer_enabled));
        service->UpdateRateLimit(info.max_transmit_speed);
    }
}

void RenderModuleRegistry::VisitWebRtcLibraries(const std::function<void(const std::shared_ptr<WebRtcTransportHandle>&)>& visitor) {
    for (const auto& library : SnapshotWebRtcLibraries()) {
        if (visitor) {
            visitor(library);
        }
    }
}

FileTransferSendResult RenderModuleRegistry::SendFileTransferMessageOnRoute(const std::string& transport_id, const std::string& stream_id,
                                                                            const std::shared_ptr<Data>& message, const std::string& connection_id) {
    FileTransferSendResult result = FileTransferSendResult::Disconnected("requested file-transfer transport is unavailable");
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        relay = relay_transport_;
    }
    if (ws && ws->Id() == transport_id) {
        result = ws->SendFileTransfer(stream_id, message, false, connection_id);
    } else if (relay && relay->Id() == transport_id) {
        result = relay->SendFileTransfer(stream_id, message, false, connection_id);
    }
    VisitWebRtcLibraries([&](const std::shared_ptr<WebRtcTransportHandle>& library) {
        if (library->Info().id == transport_id) {
            result = library->SendFileTransfer(stream_id, message, false, connection_id);
        }
    });
    return result;
}

bool RenderModuleRegistry::SendControlMessageOnRoute(const std::string& transport_id, const std::string& stream_id,
                                                     const std::shared_ptr<Data>& message, const bool run_through) {
    bool sent = false;
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<UdpTransport> udp;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        udp = udp_transport_;
        relay = relay_transport_;
    }
    if (ws && ws->Id() == transport_id) {
        sent = ws->SendToStream(stream_id, message, run_through);
    } else if (udp && udp->Id() == transport_id) {
        sent = udp->SendToStream(stream_id, message, run_through);
    } else if (relay && relay->Id() == transport_id) {
        sent = relay->SendToStream(stream_id, message, run_through);
    }
    VisitWebRtcLibraries([&](const std::shared_ptr<WebRtcTransportHandle>& library) {
        if (library->Info().id == transport_id) {
            sent = library->SendToStream(stream_id, message, run_through);
        }
    });
    return sent;
}

bool RenderModuleRegistry::SendVoiceMessageOnRoute(const std::string& transport_id, const std::string& stream_id,
                                                   const std::shared_ptr<Data>& message) {
    if (!message || stream_id.empty()) {
        return false;
    }
    bool delivered = false;
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<UdpTransport> udp;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        udp = udp_transport_;
        relay = relay_transport_;
    }
    const auto send_selected = [&](const auto& transport) {
        if (transport && (transport_id.empty() || transport->Id() == transport_id)) {
            delivered = transport->SendToStream(stream_id, message, true) || delivered;
        }
    };
    send_selected(ws);
    send_selected(udp);
    send_selected(relay);
    VisitWebRtcLibraries([&](const std::shared_ptr<WebRtcTransportHandle>& library) {
        if (transport_id.empty() || library->Info().id == transport_id) {
            delivered = library->SendToStream(stream_id, message, true) || delivered;
        }
    });
    if (!delivered && !transport_id.empty()) {
        const auto send_fallback = [&](const auto& transport) {
            if (transport) {
                delivered = transport->SendToStream(stream_id, message, true) || delivered;
            }
        };
        send_fallback(ws);
        send_fallback(udp);
        send_fallback(relay);
        VisitWebRtcLibraries(
            [&](const std::shared_ptr<WebRtcTransportHandle>& library) { delivered = library->SendToStream(stream_id, message, true) || delivered; });
    }
    return delivered;
}

bool RenderModuleRegistry::SetRtcVoiceAuthorizationOnRoute(const std::string& stream_id, const std::string& call_id, const bool authorized) {
    std::shared_ptr<WebRtcTransportHandle> rtc_local;
    {
        std::shared_lock lock(modules_mtx_);
        rtc_local = rtc_local_transport_;
    }
    return rtc_local && rtc_local->SetVoiceAuthorization(stream_id, call_id, authorized);
}

bool RenderModuleRegistry::SendRtcVoicePcmOnRoute(const std::string& stream_id, const std::string& call_id,
                                                  const std::shared_ptr<const std::vector<std::int16_t>>& samples, const int sample_rate,
                                                  const int channels) {
    if (!samples || samples->empty()) {
        return false;
    }
    std::shared_ptr<WebRtcTransportHandle> rtc_local;
    {
        std::shared_lock lock(modules_mtx_);
        rtc_local = rtc_local_transport_;
    }
    return rtc_local && rtc_local->SubmitVoicePcm(stream_id, call_id, samples, sample_rate, channels);
}

int64_t RenderModuleRegistry::QueuedNetworkMediaMessages() {
    int64_t queuing_msg_count = 0;
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<UdpTransport> udp;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        udp = udp_transport_;
        relay = relay_transport_;
    }
    if (ws && ws->ConnectedClientCount() > 0) {
        queuing_msg_count += ws->QueuedMediaCount();
    }
    if (relay && relay->ConnectedClientCount() > 0) {
        queuing_msg_count += relay->QueuedMediaCount();
    }
    static_cast<void>(udp);
    VisitWebRtcLibraries([&](const std::shared_ptr<WebRtcTransportHandle>& library) {
        if (library->ConnectedClientCount() > 0) {
            queuing_msg_count += library->QueuedMediaMessageCount();
        }
    });
    return queuing_msg_count;
}

int64_t RenderModuleRegistry::QueuedNetworkFileTransferMessages() {
    int64_t queuing_msg_count = 0;
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        relay = relay_transport_;
    }
    if (ws && ws->ConnectedClientCount() > 0) {
        queuing_msg_count += ws->QueuedFileTransferCount();
    }
    if (relay && relay->ConnectedClientCount() > 0) {
        queuing_msg_count += relay->QueuedFileTransferCount();
    }
    VisitWebRtcLibraries([&](const std::shared_ptr<WebRtcTransportHandle>& library) {
        if (library->ConnectedClientCount() > 0) {
            queuing_msg_count += library->QueuedFileTransferMessageCount();
        }
    });
    return queuing_msg_count;
}

int RenderModuleRegistry::GetTotalConnectedClientsCount() {
    int total_size = 0;
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<UdpTransport> udp;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        udp = udp_transport_;
        relay = relay_transport_;
    }
    if (ws)
        total_size += ws->ConnectedClientCount();
    if (udp)
        total_size += udp->ConnectedClientCount();
    if (relay)
        total_size += relay->ConnectedClientCount();
    VisitWebRtcLibraries([&](const std::shared_ptr<WebRtcTransportHandle>& library) { total_size += library->ConnectedClientCount(); });
    return total_size;
}

int RenderModuleRegistry::GetTotalMediaConsumersCount() {
    int total_size = 0;
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<UdpTransport> udp;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        udp = udp_transport_;
        relay = relay_transport_;
    }
    if (ws)
        total_size += ws->ConnectedClientCount();
    if (udp)
        total_size += udp->ConnectedClientCount();
    if (relay)
        total_size += relay->ConnectedClientCount();
    VisitWebRtcLibraries([&](const std::shared_ptr<WebRtcTransportHandle>& library) {
        total_size += library->Kind() == WebRtcTransportKind::kLocal ? library->MediaConsumerCount() : library->ConnectedClientCount();
    });
    return total_size;
}

std::vector<std::shared_ptr<PxConnectedClientInfo>> RenderModuleRegistry::GetConnectedClientsInfo() {
    std::vector<std::shared_ptr<PxConnectedClientInfo>> clients_info;
    std::shared_ptr<WsTransport> ws;
    std::shared_ptr<RelayTransport> relay;
    {
        std::shared_lock lock(modules_mtx_);
        ws = ws_transport_;
        relay = relay_transport_;
    }
    if (ws) {
        for (const auto& info : ws->ConnectedClients()) {
            clients_info.push_back(info);
        }
    }
    if (relay) {
        for (const auto& info : relay->ConnectedClients()) {
            clients_info.push_back(info);
        }
    }
    VisitWebRtcLibraries([&](const std::shared_ptr<WebRtcTransportHandle>& library) {
        for (const auto& info : library->ConnectedClients()) {
            clients_info.push_back(info);
        }
    });
    return clients_info;
}

// is GDI
bool RenderModuleRegistry::IsGdiCapture(const std::shared_ptr<MonitorCaptureSource>& source) {
    return source && source->Id() == kGdiCaptureSourceId;
}

// is DDA
bool RenderModuleRegistry::IsDdaCapture(const std::shared_ptr<MonitorCaptureSource>& source) {
    return source && source->Id() == kDdaCaptureSourceId;
}

} // namespace px
