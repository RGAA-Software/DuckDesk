//
// Created by RGAA on 21/11/2024.
//

#ifndef PX_NET_PLUGIN_H
#define PX_NET_PLUGIN_H

#include "px_plugin_interface.h"
#include "px_net_plugin_type.h"
#include "px_render/network/transport_types.h"
#include <vector>

namespace px
{

    class Data;
    class MsgRtcRemoteIce;
    class MsgRtcRemoteSdp;
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

        // Typed control-plane commands. These are intentionally appended to
        // the compatibility vtable: Render no longer multiplexes unrelated
        // network commands through an untyped catch-all entry point.
        virtual void ApplyRtcRemoteSdp(const MsgRtcRemoteSdp& message);
        virtual void ApplyRtcRemoteIce(const MsgRtcRemoteIce& message);
        virtual void ApplyLogicalSessionCapabilities(
            const PxLogicalSessionCapabilityUpdate& update);
        virtual void UpdateUdpMediaAssociation(
            const UdpMediaAssociation& association);

    protected:
        NetSyncInfo sync_info_{};

    };

}

#endif //PX_NET_PLUGIN_H
