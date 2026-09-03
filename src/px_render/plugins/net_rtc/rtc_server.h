//
// Created by hy on 2024/4/25.
//

#ifndef TEST_WEBRTC_RTCSERVER_H
#define TEST_WEBRTC_RTCSERVER_H

#include "px_common_new/webrtc_helper.h"
#include "video_source_mock.h"

namespace px
{
    class Data;
    class PeerCallback;
    class CreateSessCallback;
    class SetSessCallback;
    class DesktopCapture;
    class RtcPlugin;
    class RtcPluginRuntime;
    class RtcDataChannel;
    class FileTransferWritableSignal;
    class PxPluginContext;
    class PxPluginBaseEvent;

    class RtcServer : public std::enable_shared_from_this<RtcServer> {
    public:

        static std::shared_ptr<RtcServer> Make(
            const std::shared_ptr<RtcPluginRuntime>& runtime);
        explicit RtcServer(const std::shared_ptr<RtcPluginRuntime>& runtime);
        [[nodiscard]] std::shared_ptr<PxPluginContext> GetPluginContext() const;
        void DispatchEvent(const std::shared_ptr<PxPluginBaseEvent>& event) const;

        bool Start(const std::string& stream_id, const std::string& offer_sdp,
                   const std::string& ice_config_json,
                   const std::vector<std::string>& permissions);
        bool RestartWithOffer(const std::string& offer_sdp,
                              const std::string& ice_config_json,
                              const std::vector<std::string>& permissions);
        void Exit();
        void OnRemoteIce(const std::string& ice, const std::string& mid, int sdp_mline_index);
        bool IsDataChannelConnected();
        bool IsFtDataChannelConnected();

        // 插件级客户端断开事件:ICE 断开/终态、datachannel 独立关闭时触发,
        // 全连接生命周期只发一次(去重)。stream_id 用真实访客 stream id
        // (与 px::Message.stream_id 一致),不用 datachannel 内部 the_conn_id_(MD5)。
        void EmitClientDisconnectedEvent();
        void EmitFileTransferDisconnectedEvent();

        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through = false);
        bool PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through = false);
        bool PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through = false);
        [[nodiscard]] const std::string& GetStreamId() const { return stream_id_; }
        [[nodiscard]] const std::string& GetConnectionInstanceId() const {
            return connection_instance_id_;
        }
        void SetPermissions(const std::vector<std::string>& permissions) {
            permissions_ = permissions;
        }

        uint32_t GetMediaPendingMessages();
        uint32_t GetFtPendingMessages();

        bool HasEnoughBufferForQueuingMediaMessages();
        bool HasEnoughBufferForQueuingFtMessages();
        [[nodiscard]] std::shared_ptr<FileTransferWritableSignal>
        AcquireFtWritableSignal();

        void On100msTimeout();

    private:
        void CreatePeerConnectionFactory();
        void CreatePeerConnection();
        bool ApplyIceConfiguration(const std::string& ice_config_json,
                                   bool update_peer_connection);
        bool SetRemoteOffer(const std::string& offer_sdp);

        void SendSdpToRemote(const std::string& sdp);
        void SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index);

    private:
        std::shared_ptr<RtcPluginRuntime> runtime_;
        std::unique_ptr<rtc::Thread> network_thread_;
        std::unique_ptr<rtc::Thread> worker_thread_;
        std::unique_ptr<rtc::Thread> sig_thread_;
        std::string stream_id_;
        std::string connection_instance_id_;
        std::string offer_sdp_;
        std::string ice_config_json_;
        std::vector<std::string> permissions_;
        std::string sdp_;
        std::shared_ptr<PeerCallback> peer_callback_ = nullptr;
        rtc::scoped_refptr<SetSessCallback> set_remote_offer_sdp_callback_ = nullptr;
        rtc::scoped_refptr<SetSessCallback> set_local_answer_sdp_callback_ = nullptr;
        rtc::scoped_refptr<CreateSessCallback> create_answer_callback_ = nullptr;

        rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_conn_ = nullptr;
        rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_conn_factory_;
        webrtc::PeerConnectionInterface::RTCConfiguration configuration_;

        std::shared_ptr<RtcDataChannel> media_data_channel_ = nullptr;
        std::shared_ptr<RtcDataChannel> ft_data_channel_ = nullptr;
        std::shared_ptr<RtcDataChannel> input_data_channel_ = nullptr;
        std::atomic<bool> exit_ = false;
        // 断开事件去重:见 EmitClientDisconnectedEvent
        std::atomic_bool disconnect_event_sent_ = false;
    };

}

#endif //TEST_WEBRTC_RTCSERVER_H
