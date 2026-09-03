//
// Created by RGAA on 15/11/2024.
//

#include "render_module_registry.h"
#include <algorithm>
#include <mutex>
#include "rd_app.h"
#include "px_render/plugins/plugin_ids.h"
#include "rd_context.h"
#include "px_common_new/log.h"
#include "px_common_new/win32/win_helper.h"
#include "px_common_new/string_util.h"
#include "px_render/ingress/render_event_ingress.h"
#include "px_render/pipeline/encoded_video_fanout.h"
#include "settings/rd_settings.h"
#include "px_common_new/folder_util.h"
#include "px_render/plugin_interface/px_net_plugin.h"
#include "px_render/plugin_interface/px_stream_plugin.h"
#include "px_render/plugin_interface/px_plugin_interface.h"
#include "px_render/plugin_interface/px_video_encoder_plugin.h"
#include "px_render/plugin_interface/px_monitor_capture_plugin.h"
#include "architecture/services/file_transfer_service.h"
#include "architecture/services/voice_call_service.h"
#include "px_render/plugins/dda_capture/dda_capture_plugin.h"
#include "px_render/plugins/gdi_capture/gdi_capture_plugin.h"
#include "px_render/plugins/ffmpeg_encoder/ffmpeg_encoder_plugin.h"
#include "px_render/plugins/amf_encoder/amf_encoder_plugin.h"
#include "px_render/plugins/nvenc_encoder/nvenc_encoder_plugin.h"
#include "px_render/plugins/net_ws/ws_plugin.h"
#include "px_render/plugins/net_udp/udp_plugin.h"
#include "px_render/plugins/net_relay/relay_plugin.h"
#include "network/webrtc_library_host.h"

namespace px
{

    std::shared_ptr<RenderModuleRegistry> RenderModuleRegistry::Make(const std::shared_ptr<RdApplication>& app) {
        return std::make_shared<RenderModuleRegistry>(app);
    }

    RenderModuleRegistry::RenderModuleRegistry(
        const std::shared_ptr<RdApplication>& app)
        : settings_(*RdSettings::Instance()) {
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
        LOGI("plugin base path: {}", base_path);
        LOGI("plugin base data path: {}", StringUtil::ToUTF8(base_data_path));

        const PxPluginParam base_param{
            .cluster_ = {
                {"base_path", base_path},
                {"base_data_path", base_data_path},
                {"capture_audio_device_id", std::string("")},
                {"ws-listen-port", (int64_t)settings_.transmission_.listening_port_},
                {"udp-listen-port", (int64_t)settings_.transmission_.listening_port_},
                {"device_id", settings_.device_id_},
                {"direct_allow_takeover", settings_.direct_allow_takeover_},
                {"relay_device_id", settings_.relay_device_id_},
                {"relay_enabled", settings_.relay_enabled_},
                {"relay_host", settings_.relay_host_},
                {"relay_port", settings_.relay_port_},
                {"language", (int64_t)settings_.language_},
                {"appkey", settings_.appkey_},
                {"app_mode", std::string(settings_.IsGameHookMode()
                    ? "game-hook"
                    : (settings_.IsWebViewMode() ? "webview" : "desktop"))},
                {"record_auto_enabled", settings_.record_auto_},
                {"record_dir", settings_.record_dir_},
                {"record_max_segment_bytes", settings_.record_max_segment_bytes_},
                {"record_max_file_count", (int64_t)settings_.record_max_file_count_},
                {"push_enabled", settings_.push_enabled_},
                {"push_rtmp_url", settings_.push_rtmp_url_},
                {"push_audio_bitrate", (int64_t)settings_.push_audio_bitrate_},
                {"live_stream_id", settings_.live_stream_id_},
                {"push_primary_monitor", settings_.push_primary_monitor_},
            },
        };
        const auto manager_owner = shared_from_this();
        const auto register_builtin =
            [manager_owner, base_param](const std::shared_ptr<PxPluginInterface>& plugin,
                                const std::string& module_name) -> bool {
                auto param = base_param;
                param.cluster_["name"] = module_name;
                const auto plugin_id = plugin ? plugin->GetPluginId() : std::string{};
                const auto duplicate = std::ranges::any_of(
                    manager_owner->lifecycle_modules_,
                    [&plugin_id](const std::shared_ptr<PxPluginInterface>& current) {
                        return current && current->GetPluginId() == plugin_id;
                    });
                if (!plugin || plugin_id.empty() ||
                    duplicate || !plugin->OnCreate(param)) {
                    LOGE("event=module.start component=render_module_registry module={} "
                         "delivery=static outcome=failed",
                         module_name);
                    return false;
                }
                manager_owner->lifecycle_modules_.push_back(plugin);
                LOGI("event=module.start component=render_module_registry module={} "
                     "delivery=static outcome=success",
                     module_name);
                return true;
            };
        const auto dda_capture = std::make_shared<DDACapturePlugin>();
        const auto weak_registry = weak_from_this();
        dda_capture->ConfigureMediaBacklogProbe([weak_registry]() {
            const auto registry = weak_registry.lock();
            return registry
                ? registry->GetQueuingMediaMsgCountInNetPlugins()
                : std::int64_t{0};
        });
        if (register_builtin(dda_capture, "cap_dda")) {
            dda_capture_ = dda_capture;
        }
        const auto gdi_capture = std::make_shared<GdiCapturePlugin>();
        if (register_builtin(gdi_capture, "cap_gdi")) {
            gdi_capture_ = gdi_capture;
        }
        const auto ffmpeg_encoder = std::make_shared<FFmpegEncoderPlugin>();
        if (register_builtin(ffmpeg_encoder, "enc_ffmpeg")) {
            ffmpeg_encoder_ = ffmpeg_encoder;
            encoders_.push_back(ffmpeg_encoder);
        }
        const auto amf_encoder = std::make_shared<AmfEncoderPlugin>();
        if (register_builtin(amf_encoder, "enc_amf")) {
            amf_encoder_ = amf_encoder;
            encoders_.push_back(amf_encoder);
        }
        const auto nvenc_encoder = std::make_shared<NvencEncoderPlugin>();
        if (register_builtin(nvenc_encoder, "enc_nvenc")) {
            nvenc_encoder_ = nvenc_encoder;
            encoders_.push_back(nvenc_encoder);
        }
        const auto ws_transport = std::make_shared<WsPlugin>();
        if (register_builtin(ws_transport, "net_ws")) {
            ws_transport_ = ws_transport;
            network_transports_.push_back(ws_transport);
        }
        const auto udp_transport = std::make_shared<UdpPlugin>();
        if (register_builtin(udp_transport, "net_udp")) {
            udp_transport_ = udp_transport;
            network_transports_.push_back(udp_transport);
        }
        const auto relay_transport = std::make_shared<RelayPlugin>();
        if (register_builtin(relay_transport, "net_relay")) {
            relay_transport_ = relay_transport;
            network_transports_.push_back(relay_transport);
        }

        webrtc_library_host_ = WebRtcLibraryHost::Create(
            PathFromUTF8(base_path) / "deps" / "rd_plugins");
        for (const auto& module : webrtc_library_host_->Load()) {
            if (!module) {
                continue;
            }
            auto param = base_param;
            param.cluster_["name"] = module->GetPluginId() == kNetRtcPluginId
                ? std::string("net_rtc.dll")
                : std::string("net_rtc_local.dll");
            const auto module_id = module->GetPluginId();
            const auto duplicate = std::ranges::any_of(
                lifecycle_modules_,
                [&module_id](const std::shared_ptr<PxPluginInterface>& current) {
                    return current && current->GetPluginId() == module_id;
                });
            if (duplicate || !module->OnCreate(param)) {
                LOGE("event=webrtc.module.start component=render_module_registry "
                     "module={} outcome=failed",
                     module_id);
                continue;
            }
            lifecycle_modules_.push_back(module);
            network_transports_.push_back(module);
            if (module_id == kNetRtcPluginId) {
                rtc_transport_ = module;
            }
            else if (module_id == kNetRtcLocalPluginId) {
                rtc_local_transport_ = module;
            }
            LOGI("event=webrtc.module.start component=render_module_registry "
                 "module={} version={} outcome=success",
                 module_id, module->GetVersionName());
        }

        ws_transport->ConfigureNetworkPeers(network_transports_);

    }

    void RenderModuleRegistry::BindIngressCallbacks() {
        {
            std::lock_guard lock(routing_mtx_);
            control_ingress_ = std::make_shared<RenderEventIngress>(app_);
            encoded_video_fanout_ = EncodedVideoFanout::Make(app_);
        }
        auto weak_self = weak_from_this();
        VisitAllModules([&](const std::shared_ptr<PxPluginInterface>& plugin) {
            if (plugin->GetPluginType() == PxPluginType::kEncoder ||
                plugin->GetPluginId() == kDdaCapturePluginId ||
                plugin->GetPluginId() == kGdiCapturePluginId) {
                return;
            }
            plugin->RegisterEventCallback([weak_self](const std::shared_ptr<PxPluginBaseEvent>& event) {
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
                    router->ProcessCompatibilityEvent(event);
                }
            });
        });
        for (const auto& capture : {
                 GetDdaCapture(), GetGdiCapture()}) {
            if (!capture) {
                continue;
            }
            capture->RegisterEventCallback(
                [weak_self](const std::shared_ptr<PxPluginBaseEvent>& event) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_ || !event) {
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
                if (event->event_type_ ==
                    PxPluginEventType::kPluginCapturedVideoFrameEvent) {
                    if (const auto captured = std::dynamic_pointer_cast<
                            PxPluginCapturedVideoFrameEvent>(event)) {
                        app->OnCapturedVideoFrame(captured->frame_);
                    }
                    return;
                }
                if (event->event_type_ ==
                    PxPluginEventType::kPluginCursorEvent) {
                    if (const auto cursor = std::dynamic_pointer_cast<
                            PxPluginCursorEvent>(event)) {
                        app->OnCapturedCursorBitmap(cursor->cursor_info_);
                    }
                    return;
                }
                if (router) {
                    router->ProcessCompatibilityEvent(event);
                }
            });
        }
        VisitEncoders([weak_self](
                                const std::shared_ptr<PxVideoEncoderPlugin>& encoder) {
            encoder->RegisterEventCallback(
                [weak_self](const std::shared_ptr<PxPluginBaseEvent>& event) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_ || !event ||
                    event->event_type_ !=
                        PxPluginEventType::kPluginEncodedVideoFrameEvent) {
                    return;
                }
                const auto encoded = std::dynamic_pointer_cast<
                    PxPluginEncodedVideoFrameEvent>(event);
                std::shared_ptr<EncodedVideoFanout> fanout;
                {
                    std::scoped_lock lock(self->routing_mtx_);
                    fanout = self->encoded_video_fanout_;
                }
                if (encoded && fanout) {
                    fanout->ProcessEncodedVideoFrameEvent(encoded);
                }
            });
        });
    }

    void RenderModuleRegistry::StopRouting() {
        if (exiting_.exchange(true)) {
            return;
        }
        VisitAllModules([](const std::shared_ptr<PxPluginInterface>& plugin) {
            plugin->RegisterEventCallback({});
        });
        {
            std::lock_guard lock(routing_mtx_);
            control_ingress_.reset();
            encoded_video_fanout_.reset();
            app_.reset();
        }
    }

    void RenderModuleRegistry::StopModules() {
        // reject new visitors first, the event callback registered in
        // BindIngressCallbacks also checks this flag before routing
        exiting_ = true;
        // Wait until in-flight visitors leave, then detach the explicit owners;
        // OnStop/OnDestroy run outside the lock because plugins may fire
        // events which re-enter the visiting functions
        std::vector<std::shared_ptr<PxPluginInterface>> modules;
        {
            std::unique_lock<std::shared_mutex> lock(modules_mtx_);
            modules.swap(lifecycle_modules_);
            encoders_.clear();
            network_transports_.clear();
            ffmpeg_encoder_.reset();
            nvenc_encoder_.reset();
            amf_encoder_.reset();
            dda_capture_.reset();
            gdi_capture_.reset();
            ws_transport_.reset();
            udp_transport_.reset();
            relay_transport_.reset();
            rtc_transport_.reset();
            rtc_local_transport_.reset();
        }
        for (const auto& module : modules) {
            module->OnStop();
        }
        for (const auto& module : modules) {
            module->OnDestroy();
        }
        if (webrtc_library_host_) {
            webrtc_library_host_->Reset();
            webrtc_library_host_.reset();
        }
        control_ingress_.reset();
        encoded_video_fanout_.reset();
    }

    std::shared_ptr<PxVideoEncoderPlugin>
    RenderModuleRegistry::GetFFmpegEncoder() {
        std::shared_lock lock(modules_mtx_);
        return ffmpeg_encoder_;
    }

    std::shared_ptr<PxVideoEncoderPlugin>
    RenderModuleRegistry::GetNvencEncoder() {
        std::shared_lock lock(modules_mtx_);
        return nvenc_encoder_;
    }

    std::shared_ptr<PxVideoEncoderPlugin>
    RenderModuleRegistry::GetAmfEncoder() {
        std::shared_lock lock(modules_mtx_);
        return amf_encoder_;
    }

    std::shared_ptr<PxMonitorCapturePlugin>
    RenderModuleRegistry::GetDdaCapture() {
        std::shared_lock lock(modules_mtx_);
        return dda_capture_;
    }

    std::shared_ptr<PxMonitorCapturePlugin>
    RenderModuleRegistry::GetGdiCapture() {
        std::shared_lock lock(modules_mtx_);
        return gdi_capture_;
    }

    void RenderModuleRegistry::SyncUdpInfo(
        const std::int64_t socket_fd,
        const std::string& device_id,
        const std::string& stream_id) {
        std::shared_ptr<PxNetPlugin> udp;
        {
            std::shared_lock lock(modules_mtx_);
            udp = udp_transport_;
        }
        if (udp) {
            udp->SyncInfo(NetSyncInfo{
                .socket_fd_ = socket_fd,
                .device_id_ = device_id,
                .stream_id_ = stream_id,
            });
        }
    }

    bool RenderModuleRegistry::IsRelayConnected() {
        std::shared_ptr<PxNetPlugin> relay;
        {
            std::shared_lock lock(modules_mtx_);
            relay = relay_transport_;
        }
        return relay && relay->IsWorking();
    }

    void RenderModuleRegistry::SubmitRtcLocalSharedTexture(
        const std::string& monitor_name,
        const std::uint64_t frame_index,
        const int frame_width,
        const int frame_height,
        const std::uint64_t shared_handle,
        const std::int64_t adapter_id,
        const std::uint64_t frame_format) {
        std::shared_ptr<PxNetPlugin> rtc_local;
        {
            std::shared_lock lock(modules_mtx_);
            rtc_local = rtc_local_transport_;
        }
        if (rtc_local) {
            rtc_local->OnRawVideoFrameSharedTexture(
                monitor_name, frame_index, frame_width, frame_height,
                shared_handle, adapter_id, frame_format);
        }
    }

    void RenderModuleRegistry::SubmitRtcLocalYuv(
        const std::string& monitor_name,
        const std::uint64_t frame_index,
        const int frame_width,
        const int frame_height,
        const std::shared_ptr<Image>& image) {
        std::shared_ptr<PxNetPlugin> rtc_local;
        {
            std::shared_lock lock(modules_mtx_);
            rtc_local = rtc_local_transport_;
        }
        if (rtc_local) {
            rtc_local->OnRawVideoFrameYuv(
                monitor_name, frame_index, frame_width, frame_height, image);
        }
    }

    void RenderModuleRegistry::UpdateRtcLocalCaptureMonitorInfo(
        const CaptureMonitorInfoMessage& message) {
        std::shared_ptr<PxNetPlugin> rtc_local;
        {
            std::shared_lock lock(modules_mtx_);
            rtc_local = rtc_local_transport_;
        }
        if (rtc_local) {
            rtc_local->UpdateCaptureMonitorInfo(message);
        }
    }

    void RenderModuleRegistry::ApplyRtcLocalRemoteSdp(
        const MsgRtcRemoteSdp& message) {
        std::shared_ptr<PxNetPlugin> rtc_local;
        {
            std::shared_lock lock(modules_mtx_);
            rtc_local = rtc_local_transport_;
        }
        if (rtc_local) {
            rtc_local->ApplyRtcRemoteSdp(message);
        }
    }

    void RenderModuleRegistry::ApplyRtcLocalRemoteIce(
        const MsgRtcRemoteIce& message) {
        std::shared_ptr<PxNetPlugin> rtc_local;
        {
            std::shared_lock lock(modules_mtx_);
            rtc_local = rtc_local_transport_;
        }
        if (rtc_local) {
            rtc_local->ApplyRtcRemoteIce(message);
        }
    }

    void RenderModuleRegistry::DispatchRtcLocalMessage(
        const std::shared_ptr<Message>& message) {
        std::shared_ptr<PxNetPlugin> rtc_local;
        {
            std::shared_lock lock(modules_mtx_);
            rtc_local = rtc_local_transport_;
        }
        if (rtc_local) {
            rtc_local->OnMessage(message);
        }
    }

    void RenderModuleRegistry::BroadcastNetworkMessage(
        const std::shared_ptr<Data>& message, const bool run_through) {
        if (!message) {
            return;
        }
        VisitNetworkTransports(
            [message, run_through](const std::shared_ptr<PxNetPlugin>& transport) {
                transport->PostProtoMessage(message, run_through);
            });
    }

    void RenderModuleRegistry::BroadcastTargetStreamMessage(
        const std::string& stream_id,
        const std::shared_ptr<Data>& message,
        const bool run_through) {
        if (!message) {
            return;
        }
        VisitNetworkTransports(
            [&stream_id, &message, run_through](
                const std::shared_ptr<PxNetPlugin>& transport) {
                static_cast<void>(transport->PostTargetStreamProtoMessage(
                    stream_id, message, run_through));
            });
    }

    void RenderModuleRegistry::BroadcastFileTransferMessage(
        const std::string& stream_id,
        const std::shared_ptr<Data>& message,
        const bool run_through) {
        if (!message) {
            return;
        }
        VisitNetworkTransports(
            [&stream_id, &message, run_through](
                const std::shared_ptr<PxNetPlugin>& transport) {
                static_cast<void>(transport->PostTargetFileTransferProtoMessage(
                    stream_id, message, run_through));
            });
    }

    void RenderModuleRegistry::BroadcastRawAudio(
        const std::shared_ptr<Data>& data,
        const int samples,
        const int channels,
        const int bits) {
        if (!data) {
            return;
        }
        VisitNetworkTransports(
            [&data, samples, channels, bits](
                const std::shared_ptr<PxNetPlugin>& transport) {
                transport->OnRawAudioData(data, samples, channels, bits);
            });
    }

    void RenderModuleRegistry::PublishEncodedVideoMetadata(
        const std::string& monitor_name,
        const std::shared_ptr<PxPluginEncodedVideoFrameEvent>& event) {
        if (!event || !event->data_) {
            return;
        }
        VisitNetworkTransports(
            [&monitor_name, &event](
                const std::shared_ptr<PxNetPlugin>& transport) {
                transport->OnEncodedVideoFrame(
                    monitor_name, event->type_, event->data_,
                    event->frame_index_, static_cast<int>(event->frame_width_),
                    static_cast<int>(event->frame_height_), event->key_frame_);
            });
    }

    void RenderModuleRegistry::DispatchNetworkAppEvent(
        const std::shared_ptr<AppBaseEvent>& event) {
        if (!event) {
            return;
        }
        VisitNetworkTransports(
            [&event](const std::shared_ptr<PxNetPlugin>& transport) {
                transport->DispatchAppEvent(event);
            });
    }

    void RenderModuleRegistry::ApplyLogicalSessionCapabilities(
        const PxLogicalSessionCapabilityUpdate& update) {
        VisitNetworkTransports(
            [&update](const std::shared_ptr<PxNetPlugin>& transport) {
                transport->ApplyLogicalSessionCapabilities(update);
            });
    }

    bool RenderModuleRegistry::PostRtcLocalMessage(
        const std::shared_ptr<Data>& message, const bool run_through) {
        std::shared_ptr<PxNetPlugin> rtc_local;
        {
            std::shared_lock lock(modules_mtx_);
            rtc_local = rtc_local_transport_;
        }
        if (rtc_local && message && rtc_local->GetConnectedClientsCount() > 0) {
            rtc_local->PostProtoMessage(message, run_through);
            return true;
        }
        return false;
    }

    void RenderModuleRegistry::SendRelaySignalingMessage(
        const std::string& stream_id,
        const std::shared_ptr<Data>& message) {
        std::shared_ptr<PxNetPlugin> relay;
        {
            std::shared_lock lock(modules_mtx_);
            relay = relay_transport_;
        }
        if (!relay || !message) {
            return;
        }
        if (stream_id.empty()) {
            relay->PostProtoMessage(message, true);
            return;
        }
        static_cast<void>(
            relay->PostTargetStreamProtoMessage(stream_id, message, true));
    }

    void RenderModuleRegistry::PostWsIpcBinaryMessage(
        const std::shared_ptr<Data>& message) {
        std::shared_ptr<PxNetPlugin> ws;
        {
            std::shared_lock lock(modules_mtx_);
            ws = ws_transport_;
        }
        if (ws && message) {
            ws->PostIpcBinaryMessage(message);
        }
    }

    void RenderModuleRegistry::RegisterWsIpcPid(const std::uint32_t pid) {
        std::shared_ptr<PxNetPlugin> ws;
        {
            std::shared_lock lock(modules_mtx_);
            ws = ws_transport_;
        }
        if (ws) {
            ws->RegisterIpcPid(pid);
        }
    }

    void RenderModuleRegistry::PostWsUserProxyMessage(
        const std::shared_ptr<Data>& message) {
        std::shared_ptr<PxNetPlugin> ws;
        {
            std::shared_lock lock(modules_mtx_);
            ws = ws_transport_;
        }
        if (ws && message) {
            ws->PostUserProxyMessage(message);
        }
    }

    bool RenderModuleRegistry::IsWsUserProxyConnected() {
        std::shared_ptr<PxNetPlugin> ws;
        {
            std::shared_lock lock(modules_mtx_);
            ws = ws_transport_;
        }
        return ws && ws->IsUserProxyConnected();
    }

    bool RenderModuleRegistry::HasWorkingVideoClient() {
        for (const auto& transport : SnapshotNetworkTransports()) {
            if (transport->IsWorking() && !transport->IsOnlyAudioClients()) {
                return true;
            }
        }
        return false;
    }

    std::vector<RenderModuleInfo> RenderModuleRegistry::SnapshotModuleInfo() {
        std::vector<RenderModuleInfo> result;
        const auto modules = SnapshotLifecycleModules();
        result.reserve(modules.size());
        for (const auto& module : modules) {
            result.push_back(RenderModuleInfo{
                .id = module->GetPluginId(),
                .name = module->GetPluginName(),
                .author = module->GetPluginAuthor(),
                .description = module->GetPluginDescription(),
                .version_name = module->GetVersionName(),
                .version_code = module->GetVersionCode(),
                .enabled = module->IsPluginEnabled(),
            });
        }
        return result;
    }

    bool RenderModuleRegistry::SetModuleEnabled(
        const std::string& module_id, const bool enabled) {
        for (const auto& module : SnapshotLifecycleModules()) {
            if (module->GetPluginId() != module_id) {
                continue;
            }
            LOGI("event=module.enable component=render_module_registry "
                 "module={} enabled={}",
                 module->GetPluginName(), enabled);
            if (enabled) {
                module->EnablePlugin();
            }
            else {
                module->DisablePlugin();
            }
            return true;
        }
        return false;
    }

    void RenderModuleRegistry::DispatchAppEventToModules(
        const std::shared_ptr<AppBaseEvent>& event) {
        if (!event) {
            return;
        }
        VisitAllModules(
            [&event](const std::shared_ptr<PxPluginInterface>& module) {
                module->DispatchAppEvent(event);
            });
    }

    void RenderModuleRegistry::UpdateModuleD3DResources(
        const std::uint64_t adapter_uid,
        const Microsoft::WRL::ComPtr<ID3D11Device>& device,
        const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context) {
        VisitAllModules(
            [adapter_uid, device, context](
                const std::shared_ptr<PxPluginInterface>& module) {
                module->d3d11_devices_[adapter_uid] = device;
                module->d3d11_devices_context_[adapter_uid] = context;
            });
    }

    void RenderModuleRegistry::ClearModuleD3DResources(
        const std::uint64_t adapter_uid) {
        VisitAllModules(
            [adapter_uid](const std::shared_ptr<PxPluginInterface>& module) {
                module->d3d11_devices_.erase(adapter_uid);
                module->d3d11_devices_context_.erase(adapter_uid);
            });
    }

    void RenderModuleRegistry::InsertIdr(
        const std::string& monitor_name) {
        VisitEncoders(
            [&monitor_name](
                const std::shared_ptr<PxVideoEncoderPlugin>& encoder) {
                encoder->InsertIdr(monitor_name);
            });
    }

    bool RenderModuleRegistry::InvalidateReferenceFrame(
        const std::string& monitor_name,
        const std::uint64_t invalid_frame_index) {
        bool accepted = false;
        VisitEncoders(
            [&accepted, &monitor_name, invalid_frame_index](
                const std::shared_ptr<PxVideoEncoderPlugin>& encoder) {
                accepted = encoder->InvalidateRefFrame(
                    monitor_name, invalid_frame_index) || accepted;
            });
        return accepted;
    }

    std::vector<std::shared_ptr<PxPluginInterface>>
    RenderModuleRegistry::SnapshotLifecycleModules() {
        std::shared_lock lock(modules_mtx_);
        return lifecycle_modules_;
    }

    std::vector<std::shared_ptr<PxVideoEncoderPlugin>>
    RenderModuleRegistry::SnapshotEncoders() {
        std::shared_lock lock(modules_mtx_);
        return encoders_;
    }

    void RenderModuleRegistry::VisitAllModules(const std::function<void(
        const std::shared_ptr<PxPluginInterface>&)>& visitor) {
        for (const auto& plugin : SnapshotLifecycleModules()) {
            if (visitor) {
                visitor(plugin);
            }
        }
    }

    void RenderModuleRegistry::VisitEncoders(const std::function<void(
        const std::shared_ptr<PxVideoEncoderPlugin>&)>& visitor) {
        for (const auto& encoder : SnapshotEncoders()) {
            if (visitor) {
                visitor(encoder);
            }
        }
    }

    std::vector<std::shared_ptr<PxNetPlugin>>
    RenderModuleRegistry::SnapshotNetworkTransports() {
        std::shared_lock lock(modules_mtx_);
        return network_transports_;
    }

    void RenderModuleRegistry::VisitNetworkTransports(const std::function<void(
        const std::shared_ptr<PxNetPlugin>&)>& visitor) {
        for (const auto& transport : SnapshotNetworkTransports()) {
            if (visitor) {
                visitor(transport);
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

            // hold the shared lock for the whole visit, so StopModules
            // cannot destroy the plugins while they are in use here
            std::shared_lock<std::shared_mutex> lock(self->modules_mtx_);
            if (self->lifecycle_modules_.empty()) {
                return;
            }

            const auto modules_snapshot = self->lifecycle_modules_;
            const auto transports_snapshot = self->network_transports_;

            int media_consumer_count = 0;
            for (const auto& transport : transports_snapshot) {
                if (self->exiting_) {
                    return;
                }
                // GetMediaConsumersCount is a tail ABI extension currently
                // implemented only by RTC Local. Other transports use their
                // ordinary connected-client count.
                media_consumer_count +=
                    transport->GetPluginId() == kNetRtcLocalPluginId
                    ? transport->GetMediaConsumersCount()
                    : transport->GetConnectedClientsCount();
            }

            //LOGI("connected_client_count: {}", connected_client_count);
            for (const auto& plugin : modules_snapshot) {
                if (self->exiting_) {
                    return;
                }
                plugin->On1Second();

                // connected clients count
                {
                    auto event = std::make_shared<MsgConnectedClientCount>();
                    // This event drives capture/encoder idle state. It counts
                    // hidden media observers too, while public statistics use
                    // GetTotalConnectedClientsCount below.
                    event->connected_client_count_ = media_consumer_count;
                    plugin->DispatchAppEvent(event);
                }
            }
        });
    }

    void RenderModuleRegistry::DumpModuleInfo() {
        std::shared_lock lock(modules_mtx_);
        LOGI("====> Total modules: {}", lifecycle_modules_.size());
        int index = 1;
        for (const auto& plugin : lifecycle_modules_) {
            LOGI("Plugin {}. [{}] vn: [{}], vc: [{}], enabled: [{}]",
                 index++,
                 plugin->GetPluginName(),
                 plugin->GetVersionName(),
                 plugin->GetVersionCode(),
                 plugin->IsPluginEnabled()
            );
        }
    }

    void RenderModuleRegistry::SyncPluginSettingsInfo(const PxPluginSettingsInfo& info) {
        if (exiting_) {
            return;
        }
        VisitAllModules([&](const std::shared_ptr<PxPluginInterface>& plugin) {
            plugin->OnSyncPluginSettingsInfo(info);
        });
        if (const auto service = context_->GetFileTransferService()) {
            static_cast<void>(service->SetEnabled(info.file_transfer_enabled_));
            service->UpdateRateLimit(info.max_transmit_speed_);
        }
    }

    FileTransferSendResult RenderModuleRegistry::SendFileTransferMessageOnRoute(
        const std::string& transport_id,
        const std::string& stream_id,
        const std::shared_ptr<Data>& message,
        const std::string& connection_id) {
        FileTransferSendResult result = FileTransferSendResult::Disconnected(
            "requested file-transfer transport is unavailable");
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            if (plugin && plugin->GetPluginId() == transport_id) {
                result = plugin->PostTargetFileTransferProtoMessage(
                    stream_id, message, false, connection_id);
            }
        });
        return result;
    }

    bool RenderModuleRegistry::SendControlMessageOnRoute(
        const std::string& transport_id,
        const std::string& stream_id,
        const std::shared_ptr<Data>& message,
        const bool run_through) {
        bool sent = false;
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            if (plugin && plugin->GetPluginId() == transport_id) {
                sent = plugin->PostTargetStreamProtoMessage(
                    stream_id, message, run_through);
            }
        });
        return sent;
    }

    bool RenderModuleRegistry::SendVoiceMessageOnRoute(
        const std::string& transport_id,
        const std::string& stream_id,
        const std::shared_ptr<Data>& message) {
        if (!message || stream_id.empty()) {
            return false;
        }
        bool delivered = false;
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            if (!plugin || (!transport_id.empty() &&
                            plugin->GetPluginId() != transport_id)) {
                return;
            }
            delivered = plugin->PostTargetStreamProtoMessage(
                stream_id, message, true) || delivered;
        });
        if (!delivered && !transport_id.empty()) {
            VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
                if (plugin) {
                    delivered = plugin->PostTargetStreamProtoMessage(
                        stream_id, message, true) || delivered;
                }
            });
        }
        return delivered;
    }

    bool RenderModuleRegistry::SetRtcVoiceAuthorizationOnRoute(
        const std::string& stream_id,
        const std::string& call_id,
        const bool authorized) {
        bool applied = false;
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            if (plugin && plugin->GetPluginId() == kNetRtcLocalPluginId) {
                applied = plugin->SetVoiceCallAuthorization(
                    stream_id, call_id, authorized) || applied;
            }
        });
        return applied;
    }

    bool RenderModuleRegistry::SendRtcVoicePcmOnRoute(
        const std::string& stream_id,
        const std::string& call_id,
        const std::shared_ptr<const std::vector<std::int16_t>>& samples,
        const int sample_rate,
        const int channels) {
        if (!samples || samples->empty()) {
            return false;
        }
        bool delivered = false;
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            if (plugin && plugin->GetPluginId() == kNetRtcLocalPluginId) {
                // NOLINTNEXTLINE(gammaray-raw-pointer-boundary): synchronous libwebrtc PCM ABI
                plugin->OnVoiceCallPcm(
                    stream_id, call_id, samples->data(), samples->size(),
                    sample_rate, channels);
                delivered = true;
            }
        });
        return delivered;
    }

    int64_t RenderModuleRegistry::GetQueuingMediaMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingMediaMsgCount();
            }
        });
        return queuing_msg_count;
    }

    int64_t RenderModuleRegistry::GetQueuingFtMsgCountInNetPlugins() {
        int64_t queuing_msg_count = 0;
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            if (plugin->GetConnectedClientsCount() > 0) {
                queuing_msg_count += plugin->GetQueuingFtMsgCount();
            }
        });
        return queuing_msg_count;
    }

    int RenderModuleRegistry::GetTotalConnectedClientsCount() {
        int total_size = 0;
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            total_size += plugin->GetConnectedClientsCount();
        });
        return total_size;
    }

    int RenderModuleRegistry::GetTotalMediaConsumersCount() {
        int total_size = 0;
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            total_size += plugin->GetPluginId() == kNetRtcLocalPluginId
                ? plugin->GetMediaConsumersCount()
                : plugin->GetConnectedClientsCount();
        });
        return total_size;
    }

    std::vector<std::shared_ptr<PxConnectedClientInfo>> RenderModuleRegistry::GetConnectedClientsInfo() {
        std::vector<std::shared_ptr<PxConnectedClientInfo>> clients_info;
        VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            if (auto cs = plugin->GetConnectedClientInfo(); !cs.empty()) {
                for (auto& info : cs) {
                    clients_info.push_back(info);
                }
            }
        });
        return clients_info;
    }

    // is GDI
    bool RenderModuleRegistry::IsGdiCapture(
        const std::shared_ptr<PxMonitorCapturePlugin>& plugin) {
        return plugin && plugin->GetPluginId() == kGdiCapturePluginId;
    }

    // is DDA
    bool RenderModuleRegistry::IsDdaCapture(
        const std::shared_ptr<PxMonitorCapturePlugin>& plugin) {
        return plugin && plugin->GetPluginId() == kDdaCapturePluginId;
    }

}
