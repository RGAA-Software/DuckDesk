//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_WS_TRANSPORT_H
#define PX_WS_TRANSPORT_H

#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "architecture/modules/render_module.h"
#include "px_render/network/transport_types.h"

namespace px
{

    class CaptureAudioFrame;
    class CaptureVideoFrame;
    class WsServer;

    class WsTransport final : public RenderModule {
    public:
        WsTransport();
        std::string Id() const override;
        std::string Name() const override;
        std::string VersionName() const override;
        uint32_t VersionCode() const override;
        std::string Description() const override;
        RenderModuleKind Kind() const override { return RenderModuleKind::kNetwork; }
        bool Start(const RenderModuleConfiguration& configuration) override;
        bool Destroy() override;
        void ApplyLogicalSessionCapabilities(
            const PxLogicalSessionCapabilityUpdate& update);
        void Tick1Second() override;
        bool IsWorking() const override;

        void Broadcast(std::shared_ptr<Data> message, bool run_through);
        bool SendToStream(const std::string& stream_id, std::shared_ptr<Data> message, bool run_through);
        FileTransferSendResult SendFileTransfer(
            const std::string& stream_id,
            std::shared_ptr<Data> msg,
            bool run_through,
            const std::string& connection_instance_id = {});

        void SendUserProxy(std::shared_ptr<Data> message);
        bool IsUserProxyConnected();
        void SendIpc(std::shared_ptr<Data> message);
        void RegisterIpcPid(uint32_t pid);

        bool HasOnlyAudioClients();
        int ConnectedClientCount();

        int64_t QueuedMediaCount();
        int64_t QueuedFileTransferCount();
        bool HasMediaCapacity() const noexcept;
        bool HasFileTransferCapacity() const noexcept;
        std::vector<std::shared_ptr<PxConnectedClientInfo>> ConnectedClients();
        void HandleAppEvent(const std::shared_ptr<AppBaseEvent>& event) override;
        void HandleMessageAck(const std::shared_ptr<NetMessageAck>& ack);
        void ReceiveClientEvent(
            bool is_proto, std::int64_t socket_fd,
            NetPluginType transport_type, NetChannelType channel_type,
            std::shared_ptr<Data> message,
            std::string connection_instance_id = {});
        void ReceiveClientEventImmediately(
            bool is_proto, std::int64_t socket_fd,
            NetPluginType transport_type, NetChannelType channel_type,
            std::shared_ptr<Data> message,
            std::string connection_instance_id = {});
        using NetworkBroadcaster = std::function<void(
            const std::shared_ptr<Data>&, bool)>;
        using FileTransferBroadcaster = std::function<void(
            const std::string&, const std::shared_ptr<Data>&, bool)>;
        using LocalRtcCompletion = std::function<void(
            const std::shared_ptr<PxLocalRtcReplyInfo>&)>;
        using LocalRtcAllocator = std::function<PxLocalRtcAllocResult(
            const std::shared_ptr<PxLocalRtcRequestInfo>&,
            LocalRtcCompletion)>;
        using UdpAssociationUpdater = std::function<bool(
            const UdpMediaAssociation&)>;
        using IpcVideoFrameSink = std::function<void(
            const CaptureVideoFrame&)>;
        using IpcAudioFrameSink = std::function<void(
            const CaptureAudioFrame&)>;

        void ConfigureNetworkServices(
            NetworkBroadcaster network_broadcaster,
            FileTransferBroadcaster file_transfer_broadcaster,
            LocalRtcAllocator local_rtc_allocator,
            UdpAssociationUpdater udp_association_updater);
        void BroadcastNetworkMessage(
            const std::shared_ptr<Data>& message, bool run_through) const;
        void BroadcastFileTransferMessage(
            const std::string& stream_id,
            const std::shared_ptr<Data>& message,
            bool run_through) const;
        [[nodiscard]] PxLocalRtcAllocResult AllocateLocalRtcInstance(
            const std::shared_ptr<PxLocalRtcRequestInfo>& request,
            LocalRtcCompletion completion) const;
        [[nodiscard]] bool HasLocalRtcService() const;
        [[nodiscard]] bool UpdateUdpAssociation(
            const UdpMediaAssociation& association) const;
        void ConfigureIpcMediaIngress(
            IpcVideoFrameSink video_sink,
            IpcAudioFrameSink audio_sink);
        void SubmitIpcVideoFrame(const CaptureVideoFrame& frame) const;
        void SubmitIpcAudioFrame(const CaptureAudioFrame& frame) const;

        // 记录当前编码输出的显示器名(Web 端输入回放需要 monitor_name)
        void SubmitEncodedVideo(const std::string& mon_name,
                                 const PxPluginEncodedVideoType& video_type,
                                 const std::shared_ptr<Data>& data,
                                 uint64_t frame_index,
                                 int frame_width,
                                 int frame_height,
                                 bool key);
        std::string CapturingMonitorName();

    private:
        bool HasConnectedClients();

    private:
        std::shared_ptr<WsServer> ws_server_ = nullptr;
        std::shared_ptr<NetMessageAck> last_ack_ = nullptr;
        std::mutex capturing_mon_mtx_;
        std::string capturing_mon_name_;
        mutable std::mutex network_services_mutex_;
        NetworkBroadcaster network_broadcaster_;
        FileTransferBroadcaster file_transfer_broadcaster_;
        LocalRtcAllocator local_rtc_allocator_;
        UdpAssociationUpdater udp_association_updater_;
        mutable std::mutex ipc_media_ingress_mutex_;
        IpcVideoFrameSink ipc_video_frame_sink_;
        IpcAudioFrameSink ipc_audio_frame_sink_;
        // exe 侧通过插件参数下发("app_mode");DLL 内的 RdSettings 单例是独立副本不可用
        bool game_hook_mode_ = false;
    };

}


#endif  // PX_WS_TRANSPORT_H
