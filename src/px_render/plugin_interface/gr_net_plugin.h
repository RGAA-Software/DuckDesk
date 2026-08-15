//
// Created by RGAA on 21/11/2024.
//

#ifndef GAMMARAY_GR_NET_PLUGIN_H
#define GAMMARAY_GR_NET_PLUGIN_H

#include "gr_plugin_interface.h"
#include "gr_net_plugin_type.h"
#include <vector>

namespace tc
{

    class Data;

    class NetSyncInfo {
    public:
        int64_t socket_fd_{0};
        std::string device_id_{};
        std::string stream_id_{};
    };

    // connected client information
    class GrConnectedClientInfo {
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
    enum class GrLocalRtcContentType {
        kDesktop,
        kGameStream,
    };

    class GrLocalRtcRequestInfo {
    public:
        std::string device_id_;
        std::string stream_id_;
        std::string req_ip_;
        std::string sdp_;
        GrLocalRtcContentType content_type_;
        // true: 调用方已确认接管,直接顶掉同 stream_id 的现存连接;
        // false: 若现存连接仍活跃,返回 kOccupied 让调用方去确认
        bool takeover_ = false;
        // 浏览器 nonce(web client 经 launch 页带入)。与现存活跃连接的
        // nonce 相同 = 同一浏览器重复打开,信令视为自动接管,不报 kOccupied
        std::string client_nonce_;
    };

    // alloc result of a local rtc instance
    enum class GrLocalRtcAllocResult {
        kOk,
        // 同 stream_id 的连接仍在活跃,且未指定 takeover
        kOccupied,
        kFailed,
    };

    // local webrtc reply info
    class GrLocalRtcMonitorInfo {
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

    class GrLocalRtcReplyInfo {
    public:
        std::string answer_sdp_;
        // 显示器列表(枚举顺序,与 video track 顺序一致),供多 track 客户端做
        // track→mon_name 映射;web/旧客户端忽略此字段
        std::vector<GrLocalRtcMonitorInfo> monitors_;
    };

    class GrNetPlugin : public GrPluginInterface {
    public:
        GrNetPlugin();
        ~GrNetPlugin() override;

        // Serialized proto message from Renderer
        // to see format details in px_message_new/tc_message.proto
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
        virtual bool PostTargetFileTransferProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through);

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
                               std::shared_ptr<Data> msg);

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

        virtual std::vector<std::shared_ptr<GrConnectedClientInfo>> GetConnectedClientInfo();

        // alloc a new local rtc server
        virtual GrLocalRtcAllocResult AllocNewLocalRtcInstance(const std::shared_ptr<GrLocalRtcRequestInfo>& info,
                                                               std::function<void(const std::shared_ptr<GrLocalRtcReplyInfo>&)>&& callback) {
            return GrLocalRtcAllocResult::kFailed;
        }

        // message ack
        virtual void OnMessageAck(const std::shared_ptr<NetMessageAck>& ack) {

        }

        // Appended at end to avoid shifting existing GrNetPlugin vtable slots.
        // Binary CaptureMessage blob to injected game DLL over /ipc (game-hook input).
        virtual void PostIpcBinaryMessage(std::shared_ptr<Data> msg);

        // Appended after PostIpcBinaryMessage (same vtable-slot caution applies).
        // Register a game pid that is allowed to connect /ipc (written boot config for it).
        virtual void RegisterIpcPid(uint32_t pid);

    protected:
        NetSyncInfo sync_info_{};

    };

}

#endif //GAMMARAY_GR_NET_PLUGIN_H
