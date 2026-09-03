//
// Created by RGAA on 21/11/2024.
//

#ifndef PX_NET_PLUGIN_H
#define PX_NET_PLUGIN_H

#include "px_plugin_interface.h"
#include "px_net_plugin_type.h"
#include <vector>

namespace px
{

    class Data;

    class NetSyncInfo {
    public:
        int64_t socket_fd_{0};
        std::string device_id_{};
        std::string stream_id_{};
    };

    // connected client information
    class PxConnectedClientInfo {
    public:
        std::string device_id_;
        // direct mode
        std::string stream_id_;
        // relay mode
        std::string relay_room_id_;
        // device name. like: DESKTOP-N3GIEVQ
        std::string device_name_;
    };

    // local webrtc request info
    enum class PxLocalRtcContentType {
        kDesktop,
        kGameStream,
    };

    // Local RTC session purpose. Wall observers are trusted, read-only Console
    // monitoring sessions: they consume video but are deliberately excluded
    // from the normal visitor lifecycle, controls, audio and audit statistics.
    enum class PxLocalRtcSessionRole {
        kInteractive,
        // Visible read-only product observer. Unlike the internal wall
        // observer it retains normal audio and lifecycle accounting.
        kObserver,
        kWallObserver,
    };

    class PxLocalRtcRequestInfo {
    public:
        std::string device_id_;
        std::string stream_id_;
        std::string req_ip_;
        std::string sdp_;
        PxLocalRtcContentType content_type_;
        PxLocalRtcSessionRole session_role_ = PxLocalRtcSessionRole::kInteractive;
        // true: 调用方已确认接管,直接顶掉同 stream_id 的现存连接;
        // false: 若现存连接仍活跃,返回 kOccupied 让调用方去确认
        bool takeover_ = false;
        // 浏览器 nonce(web client 经 launch 页带入)。与现存活跃连接的
        // nonce 相同 = 同一浏览器重复打开,信令视为自动接管,不报 kOccupied
        std::string client_nonce_;
        // Server-issued capability snapshot from a consumed Console ticket.
        // Empty permissions still mean no capabilities when this flag is true.
        bool capability_enforced_ = false;
        std::vector<std::string> permissions_;
    };

    // alloc result of a local rtc instance
    enum class PxLocalRtcAllocResult {
        kOk,
        // 同 stream_id 的连接仍在活跃,且未指定 takeover
        kOccupied,
        kFailed,
    };

    // local webrtc reply info
    class PxLocalRtcMonitorInfo {
    public:
        std::string name_;
        int width_ = 0;
        int height_ = 0;
        // 虚拟桌面坐标,客户端多屏布局/鼠标坐标映射用
        int left_ = 0;
        int top_ = 0;
        int right_ = 0;
        int bottom_ = 0;
    };

    class PxLocalRtcReplyInfo {
    public:
        std::string answer_sdp_;
        // 显示器列表(枚举顺序,与 video track 顺序一致),供多 track 客户端做
        // track→mon_name 映射;web/旧客户端忽略此字段
        std::vector<PxLocalRtcMonitorInfo> monitors_;
    };

    // Reliable-control-plane notification used to revoke a previously granted
    // session capability after a Controller is replaced. Network plug-ins use
    // the stream only as a routing key; role ownership remains in
    // LogicalSessionRegistry.
    class PxLogicalSessionCapabilityUpdate {
    public:
        std::string stream_id_;
        std::vector<std::string> permissions_;
    };

    class PxNetPlugin : public PxPluginInterface {
    public:
        PxNetPlugin();
        ~PxNetPlugin() override;

        // Serialized proto message from Renderer
        // to see format details in px_message_new/px_message.proto
        // such as : message VideoFrame { ... }
        // you can send it to any clients
        //                       -> client 1
        // Renderer Messages ->  -> client 2
        //                       -> client 3
        // run_through: send the message even if stream was paused
        virtual void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through);

        // Serialized proto message from Renderer
        // to a specific stream
        virtual bool PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through);

        // Serialized proto message from Renderer
        // to file transfer
        virtual FileTransferSendResult PostTargetFileTransferProtoMessage(
            const std::string& stream_id,
            std::shared_ptr<Data> msg,
            bool run_through,
            const std::string& connection_instance_id = {});

        // Serialized RpMessage to localhost UserProxy WebSocket
        virtual void PostUserProxyMessage(std::shared_ptr<Data> msg);

        virtual bool IsUserProxyConnected();

        // messages from remote(client) -> this plugin -> exe processes it
        // client 1 ->
        // client 2 ->  -> Renderer
        // client 3 ->
        void OnClientEventCame(bool is_proto,
                               int64_t socket_fd,
                               const NetPluginType& nt_plugin_type,
                               const NetChannelType& ch_type,
                               std::shared_ptr<Data> msg,
                               const std::string& connection_instance_id = {});

        // Use for lightweight reliable-channel messages whose consumer only
        // validates and queues work. This avoids an extra transport work-queue
        // hop while keeping protobuf parsing out of WebRTC transport binaries.
        void OnClientEventCameDirectly(bool is_proto,
                                       int64_t socket_fd,
                                       const NetPluginType& nt_plugin_type,
                                       const NetChannelType& ch_type,
                                       std::shared_ptr<Data> msg,
                                       const std::string& connection_instance_id = {});

        virtual bool IsOnlyAudioClients();

        virtual int GetConnectedClientsCount();

        virtual void SyncInfo(const NetSyncInfo& info);

        // how many messages in queue but not be sent now
        virtual int64_t GetQueuingMediaMsgCount();

        // how many message in ft queue but not be sent now
        virtual int64_t GetQueuingFtMsgCount();

        virtual bool HasEnoughBufferForQueuingMediaMessages();
        virtual bool HasEnoughBufferForQueuingFtMessages();

        // sent data statistics
        void ReportSentDataSize(int size);

        virtual std::vector<std::shared_ptr<PxConnectedClientInfo>> GetConnectedClientInfo();

        // alloc a new local rtc server
        virtual PxLocalRtcAllocResult AllocNewLocalRtcInstance(const std::shared_ptr<PxLocalRtcRequestInfo>& info,
                                                               std::function<void(const std::shared_ptr<PxLocalRtcReplyInfo>&)>&& callback) {
            return PxLocalRtcAllocResult::kFailed;
        }

        // message ack
        virtual void OnMessageAck(const std::shared_ptr<NetMessageAck>& ack) {

        }

        // Appended at end to avoid shifting existing PxNetPlugin vtable slots.
        // Binary CaptureMessage blob to injected game DLL over /ipc (game-hook input).
        virtual void PostIpcBinaryMessage(std::shared_ptr<Data> msg);

        // Appended after PostIpcBinaryMessage (same vtable-slot caution applies).
        // Register a game pid that is allowed to connect /ipc (written boot config for it).
        virtual void RegisterIpcPid(uint32_t pid);

        // Media consumers may include non-business observers which must keep
        // capture/encoding alive without appearing as connected visitors.
        // Appended at the end to preserve the existing plugin ABI layout.
        virtual int GetMediaConsumersCount();

        // Appended for the authorized WebRTC voice path. Non-WebRTC
        // transports keep the default no-op implementation.
        virtual bool SetVoiceCallAuthorization(
            const std::string& stream_id, const std::string& call_id,
            bool authorized);
        virtual void OnVoiceCallPcm(
            const std::string& stream_id, const std::string& call_id,
            const int16_t* samples, size_t sample_count,
            int sample_rate, int channels);

    protected:
        NetSyncInfo sync_info_{};

    };

}

#endif //PX_NET_PLUGIN_H
