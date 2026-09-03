//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_MODULES_RENDER_MODULE_REGISTRY_H
#define PX_RENDER_MODULES_RENDER_MODULE_REGISTRY_H

#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include "px_render/plugins/plugin_ids.h"
#include "px_common_new/concurrent_hashmap.h"
#include "px_common_new/file_transfer_send_result.h"
#include "px_render/plugin_interface/px_plugin_settings_info.h"

namespace px
{

    class RdContext;
    class RdSettings;
    class RdApplication;
    class Data;
    class RenderEventIngress;
    class EncodedVideoFanout;
    class PxPluginInterface;
    class PxStreamPlugin;
    class PxVideoEncoderPlugin;
    class PxNetPlugin;
    class PxMonitorCapturePlugin;
    class PxFrameProcessorPlugin;
    class PxConnectedClientInfo;
    class WebRtcLibraryHost;

    class RenderModuleRegistry : public std::enable_shared_from_this<RenderModuleRegistry> {
    public:
        static std::shared_ptr<RenderModuleRegistry> Make(const std::shared_ptr<RdApplication>& app);

        explicit RenderModuleRegistry(const std::shared_ptr<RdApplication>& app);
        ~RenderModuleRegistry();

        void StartModules();
        void BindIngressCallbacks();
        void StopRouting();
        void StopModules();
        std::shared_ptr<PxVideoEncoderPlugin> GetFFmpegEncoder();
        std::shared_ptr<PxVideoEncoderPlugin> GetNvencEncoder();
        std::shared_ptr<PxVideoEncoderPlugin> GetAmfEncoder();
        std::shared_ptr<PxMonitorCapturePlugin> GetDdaCapture();
        std::shared_ptr<PxMonitorCapturePlugin> GetGdiCapture();
        std::shared_ptr<PxNetPlugin> GetRtcTransport();
        std::shared_ptr<PxNetPlugin> GetUdpTransport();
        std::shared_ptr<PxNetPlugin> GetRelayTransport();
        std::shared_ptr<PxNetPlugin> GetRtcLocalTransport();
        int64_t GetQueuingMediaMsgCountInNetPlugins();
        int64_t GetQueuingFtMsgCountInNetPlugins();
        int GetTotalConnectedClientsCount();
        int GetTotalMediaConsumersCount();
        std::vector<std::shared_ptr<PxConnectedClientInfo>> GetConnectedClientsInfo();

        void VisitAllModules(const std::function<void(
            const std::shared_ptr<PxPluginInterface>&)>& visitor);
        void VisitEncoders(const std::function<void(
            const std::shared_ptr<PxVideoEncoderPlugin>&)>& visitor);
        void VisitNetworkTransports(const std::function<void(
            const std::shared_ptr<PxNetPlugin>&)>& visitor);
        void DumpModuleInfo();

        void On1Second();

        // from render panel -> render
        void SyncPluginSettingsInfo(const PxPluginSettingsInfo& info);
        [[nodiscard]] FileTransferSendResult SendFileTransferMessageOnRoute(
            const std::string& transport_id,
            const std::string& stream_id,
            const std::shared_ptr<Data>& message,
            const std::string& connection_id);
        [[nodiscard]] bool SendControlMessageOnRoute(
            const std::string& transport_id,
            const std::string& stream_id,
            const std::shared_ptr<Data>& message,
            bool run_through);
        [[nodiscard]] bool SendVoiceMessageOnRoute(
            const std::string& transport_id,
            const std::string& stream_id,
            const std::shared_ptr<Data>& message);
        [[nodiscard]] bool SetRtcVoiceAuthorizationOnRoute(
            const std::string& stream_id,
            const std::string& call_id,
            bool authorized);
        [[nodiscard]] bool SendRtcVoicePcmOnRoute(
            const std::string& stream_id,
            const std::string& call_id,
            const std::shared_ptr<const std::vector<std::int16_t>>& samples,
            int sample_rate,
            int channels);

        // is GDI
        bool IsGdiCapture(
            const std::shared_ptr<PxMonitorCapturePlugin>& plugin);
        // is DDA
        bool IsDdaCapture(
            const std::shared_ptr<PxMonitorCapturePlugin>& plugin);

    private:
        // Process-lifetime settings singleton, represented as a non-null
        // reference so composition never carries a nullable borrowed pointer.
        RdSettings& settings_;
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        // Guards the explicit module composition. Visitors take a shared lock;
        // StopModules atomically detaches every owner under an exclusive lock.
        std::shared_mutex modules_mtx_;
        std::vector<std::shared_ptr<PxPluginInterface>> lifecycle_modules_;
        std::vector<std::shared_ptr<PxVideoEncoderPlugin>> encoders_;
        std::vector<std::shared_ptr<PxNetPlugin>> network_transports_;
        std::shared_ptr<PxVideoEncoderPlugin> ffmpeg_encoder_;
        std::shared_ptr<PxVideoEncoderPlugin> nvenc_encoder_;
        std::shared_ptr<PxVideoEncoderPlugin> amf_encoder_;
        std::shared_ptr<PxMonitorCapturePlugin> dda_capture_;
        std::shared_ptr<PxMonitorCapturePlugin> gdi_capture_;
        std::shared_ptr<PxNetPlugin> udp_transport_;
        std::shared_ptr<PxNetPlugin> relay_transport_;
        std::shared_ptr<PxNetPlugin> rtc_transport_;
        std::shared_ptr<PxNetPlugin> rtc_local_transport_;
        std::shared_ptr<WebRtcLibraryHost> webrtc_library_host_;
        std::mutex routing_mtx_;
        std::shared_ptr<RenderEventIngress> control_ingress_ = nullptr;
        std::shared_ptr<EncodedVideoFanout> encoded_video_fanout_ = nullptr;
        std::atomic_bool exiting_ = false;
    };

}

#endif  // PX_RENDER_MODULES_RENDER_MODULE_REGISTRY_H
