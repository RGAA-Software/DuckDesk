//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_WS_PLUGIN_H
#define PX_WS_PLUGIN_H

#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "px_render/plugin_interface/px_net_plugin.h"

namespace px
{

    class CaptureAudioFrame;
    class CaptureVideoFrame;
    class WsPluginServer;

    class WsPlugin : public PxNetPlugin,
                     public std::enable_shared_from_this<WsPlugin> {
    public:
        WsPlugin();
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const px::PxPluginParam& param) override;
        bool OnDestroy() override;
        void ApplyLogicalSessionCapabilities(
            const PxLogicalSessionCapabilityUpdate& update) override;
        void On1Second() override;
        bool IsWorking() override;

        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        FileTransferSendResult PostTargetFileTransferProtoMessage(
            const std::string& stream_id,
            std::shared_ptr<Data> msg,
            bool run_through,
            const std::string& connection_instance_id = {}) override;

        void PostUserProxyMessage(std::shared_ptr<Data> msg) override;
        bool IsUserProxyConnected() override;
        void PostIpcBinaryMessage(std::shared_ptr<Data> msg) override;
        void RegisterIpcPid(uint32_t pid) override;

        bool IsOnlyAudioClients() override;
        int GetConnectedClientsCount() override;

        int64_t GetQueuingMediaMsgCount() override;
        int64_t GetQueuingFtMsgCount() override;
        bool HasEnoughBufferForQueuingMediaMessages() override;
        bool HasEnoughBufferForQueuingFtMessages() override;
        std::vector<std::shared_ptr<PxConnectedClientInfo>> GetConnectedClientInfo() override;
        void DispatchAppEvent(const std::shared_ptr<AppBaseEvent> &event) override;
        void OnMessageAck(const std::shared_ptr<NetMessageAck> &ack) override;
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
        void OnEncodedVideoFrame(const std::string& mon_name,
                                 const PxPluginEncodedVideoType& video_type,
                                 const std::shared_ptr<Data>& data,
                                 uint64_t frame_index,
                                 int frame_width,
                                 int frame_height,
                                 bool key) override;
        std::string GetCapturingMonitorName();

    private:
        bool HasConnectedClients();

    private:
        std::shared_ptr<WsPluginServer> ws_server_ = nullptr;
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


#endif //PX_UDP_PLUGIN_H
