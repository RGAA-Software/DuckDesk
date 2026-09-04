//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_MODULES_RENDER_MODULE_REGISTRY_H
#define PX_RENDER_MODULES_RENDER_MODULE_REGISTRY_H

#include <functional>
#include <memory>
#include <cstdint>
#include <string>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>
#include "px_render/modules/module_ids.h"
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
    class RenderModule;
    class VideoEncoderModule;
    class MonitorCaptureSource;
    class WsTransport;
    class UdpTransport;
    class RelayTransport;
    class PxConnectedClientInfo;
    class WebRtcLibrary;
    class WebRtcLibraryHost;
    class Image;
    class Message;
    class MsgRtcRemoteIce;
    class MsgRtcRemoteSdp;
    class CaptureMonitorInfoMessage;
    class PxPluginEncodedVideoFrameEvent;
    class AppBaseEvent;
    class PxLogicalSessionCapabilityUpdate;
    class PxLocalRtcRequestInfo;
    class PxLocalRtcReplyInfo;
    enum class PxLocalRtcAllocResult;
    struct UdpMediaAssociation;

    struct RenderModuleInfo final {
        std::string id;
        std::string name;
        std::string author;
        std::string description;
        std::string version_name;
        std::uint32_t version_code{0};
        bool enabled{false};
    };

    class RenderModuleRegistry : public std::enable_shared_from_this<RenderModuleRegistry> {
    public:
        static std::shared_ptr<RenderModuleRegistry> Make(const std::shared_ptr<RdApplication>& app);

        explicit RenderModuleRegistry(const std::shared_ptr<RdApplication>& app);
        ~RenderModuleRegistry();

        void StartModules();
        void BindIngressCallbacks();
        void StopRouting();
        void StopModules();
        std::shared_ptr<VideoEncoderModule> GetFFmpegEncoder();
        std::shared_ptr<VideoEncoderModule> GetNvencEncoder();
        std::shared_ptr<VideoEncoderModule> GetAmfEncoder();
        std::shared_ptr<MonitorCaptureSource> GetDdaCapture();
        std::shared_ptr<MonitorCaptureSource> GetGdiCapture();
        void SyncUdpInfo(
            std::int64_t socket_fd,
            const std::string& device_id,
            const std::string& stream_id);
        [[nodiscard]] bool IsRelayConnected();
        void SubmitRtcLocalSharedTexture(
            const std::string& monitor_name,
            std::uint64_t frame_index,
            int frame_width,
            int frame_height,
            std::uint64_t shared_handle,
            std::int64_t adapter_id,
            std::uint64_t frame_format);
        void SubmitRtcLocalYuv(
            const std::string& monitor_name,
            std::uint64_t frame_index,
            int frame_width,
            int frame_height,
            const std::shared_ptr<Image>& image);
        void UpdateRtcLocalCaptureMonitorInfo(
            const CaptureMonitorInfoMessage& message);
        void ApplyRtcLocalRemoteSdp(const MsgRtcRemoteSdp& message);
        void ApplyRtcLocalRemoteIce(const MsgRtcRemoteIce& message);
        void DispatchRtcLocalMessage(const std::shared_ptr<Message>& message);
        [[nodiscard]] PxLocalRtcAllocResult AllocateRtcLocalInstance(
            const std::shared_ptr<PxLocalRtcRequestInfo>& request,
            std::function<void(
                const std::shared_ptr<PxLocalRtcReplyInfo>&)>&& completion);
        [[nodiscard]] bool UpdateUdpMediaAssociation(
            const UdpMediaAssociation& association);
        void BroadcastNetworkMessage(
            const std::shared_ptr<Data>& message, bool run_through);
        void BroadcastTargetStreamMessage(
            const std::string& stream_id,
            const std::shared_ptr<Data>& message,
            bool run_through);
        void BroadcastFileTransferMessage(
            const std::string& stream_id,
            const std::shared_ptr<Data>& message,
            bool run_through);
        void BroadcastRawAudio(
            const std::shared_ptr<Data>& data,
            int samples,
            int channels,
            int bits);
        void PublishEncodedVideoMetadata(
            const std::string& monitor_name,
            const std::shared_ptr<PxPluginEncodedVideoFrameEvent>& event);
        void DispatchNetworkAppEvent(
            const std::shared_ptr<AppBaseEvent>& event);
        void ApplyLogicalSessionCapabilities(
            const PxLogicalSessionCapabilityUpdate& update);
        [[nodiscard]] bool PostRtcLocalMessage(
            const std::shared_ptr<Data>& message, bool run_through);
        void SendRelaySignalingMessage(
            const std::string& stream_id,
            const std::shared_ptr<Data>& message);
        void PostWsIpcBinaryMessage(const std::shared_ptr<Data>& message);
        void RegisterWsIpcPid(std::uint32_t pid);
        void PostWsUserProxyMessage(const std::shared_ptr<Data>& message);
        [[nodiscard]] bool IsWsUserProxyConnected();
        [[nodiscard]] bool HasWorkingVideoClient();
        [[nodiscard]] std::vector<RenderModuleInfo> SnapshotModuleInfo();
        [[nodiscard]] bool SetModuleEnabled(
            const std::string& module_id, bool enabled);
        void DispatchAppEventToModules(
            const std::shared_ptr<AppBaseEvent>& event);
        void UpdateModuleD3DResources(
            std::uint64_t adapter_uid,
            const Microsoft::WRL::ComPtr<ID3D11Device>& device,
            const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context);
        void ClearModuleD3DResources(std::uint64_t adapter_uid);
        void InsertIdr(const std::string& monitor_name = {});
        [[nodiscard]] bool InvalidateReferenceFrame(
            const std::string& monitor_name,
            std::uint64_t invalid_frame_index);
        [[nodiscard]] int64_t QueuedNetworkMediaMessages();
        [[nodiscard]] int64_t QueuedNetworkFileTransferMessages();
        int GetTotalConnectedClientsCount();
        int GetTotalMediaConsumersCount();
        std::vector<std::shared_ptr<PxConnectedClientInfo>> GetConnectedClientsInfo();

        void DumpModuleInfo();

        void On1Second();

        // from render panel -> render
        void SyncModuleSettings(const PxPluginSettingsInfo& info);
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
            const std::shared_ptr<MonitorCaptureSource>& source);
        // is DDA
        bool IsDdaCapture(
            const std::shared_ptr<MonitorCaptureSource>& source);

    private:
        [[nodiscard]] std::vector<std::shared_ptr<RenderModule>>
        SnapshotModules();
        [[nodiscard]] std::vector<std::shared_ptr<VideoEncoderModule>>
        SnapshotEncoders();
        [[nodiscard]] std::vector<std::shared_ptr<WebRtcLibrary>>
        SnapshotWebRtcLibraries();
        void VisitAllModules(const std::function<void(
            const std::shared_ptr<RenderModule>&)>& operation);
        void VisitEncoders(const std::function<void(
            const std::shared_ptr<VideoEncoderModule>&)>& operation);
        void VisitWebRtcLibraries(const std::function<void(
            const std::shared_ptr<WebRtcLibrary>&)>& operation);

        // Process-lifetime settings singleton, represented as a non-null
        // reference so composition never carries a nullable borrowed pointer.
        RdSettings& settings_;
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        // Guards the explicit module composition. Visitors take a shared lock;
        // StopModules atomically detaches every owner under an exclusive lock.
        std::shared_mutex modules_mtx_;
        std::shared_ptr<VideoEncoderModule> ffmpeg_encoder_;
        std::shared_ptr<VideoEncoderModule> nvenc_encoder_;
        std::shared_ptr<VideoEncoderModule> amf_encoder_;
        std::shared_ptr<MonitorCaptureSource> dda_capture_;
        std::shared_ptr<MonitorCaptureSource> gdi_capture_;
        std::shared_ptr<WsTransport> ws_transport_;
        std::shared_ptr<UdpTransport> udp_transport_;
        std::shared_ptr<RelayTransport> relay_transport_;
        // Fixed dynamic network components. They intentionally do not enter
        // the flow-node extension graph.
        std::shared_ptr<WebRtcLibrary> rtc_transport_;
        std::shared_ptr<WebRtcLibrary> rtc_local_transport_;
        std::shared_ptr<WebRtcLibraryHost> webrtc_library_host_;
        std::mutex routing_mtx_;
        std::shared_ptr<RenderEventIngress> control_ingress_ = nullptr;
        std::shared_ptr<EncodedVideoFanout> encoded_video_fanout_ = nullptr;
        std::atomic_bool exiting_ = false;
    };

}

#endif  // PX_RENDER_MODULES_RENDER_MODULE_REGISTRY_H
