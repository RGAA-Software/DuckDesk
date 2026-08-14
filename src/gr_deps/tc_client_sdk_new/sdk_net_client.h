//
// Created by RGAA on 2023-12-27.
//

#ifndef TC_CLIENT_PC_WS_CLIENT_H
#define TC_CLIENT_PC_WS_CLIENT_H

#include "tc_message.pb.h"
#include <atomic>
#include "sdk_params.h"

namespace asio2 {
    class ws_client;
    class timer;
}

namespace tc
{

    constexpr uint32_t kMaxQueuingFtMessages = 256;

    using OnRawMessageCallback = std::function<void(std::shared_ptr<tc::Message>)>;
    using OnVideoFrameMsgCallback = std::function<void(std::shared_ptr<tc::Message>)>;
    using OnAudioFrameMsgCallback = std::function<void(std::shared_ptr<tc::Message>)>;
    using OnCursorInfoSyncMsgCallback = std::function<void(std::shared_ptr<tc::Message>)>;
    using OnAudioSpectrumCallback = std::function<void(std::shared_ptr<tc::Message>)>;
    using OnConnectedCallback = std::function<void()>;
    using OnDisconnectedCallback = std::function<void()>;
    using OnHeartBeatInfoCallback = std::function<void(std::shared_ptr<tc::Message>)>;
    using OnClipboardInfoCallback = std::function<void(std::shared_ptr<tc::Message>)>;
    using OnConfigCallback = std::function<void(std::shared_ptr<tc::Message>)>;
    using OnMonitorSwitchedCallback = std::function<void(std::shared_ptr<tc::Message>)>;

    class Data;
    class Thread;
    class MessageNotifier;
    class MessageListener;
    class Connection;
    class WebRtcConnection;
    class WebRtcLocalConnection;
    class UdpDirectConnection;
    class SdkStatistics;

    class NetClient {
    public:
        explicit NetClient(const std::shared_ptr<ThunderSdkParams>& params,
                           const std::shared_ptr<MessageNotifier>& notifier,
                           const std::string& ip,
                           int port,
                           const std::string& media_path,
                           const std::string& ft_path,
                           const ClientNetworkType& nt_type,
                           const std::string& device_id,
                           const std::string& remote_device_id,
                           const std::string& ft_device_id,
                           const std::string& ft_remote_device_id,
                           const std::string& stream_id);
        ~NetClient();

        void Start();
        void Exit();

        void PostMediaMessage(std::shared_ptr<Data> msg);
        void PostFileTransferMessage(std::shared_ptr<Data> msg);

        void SetOnVideoFrameMsgCallback(OnVideoFrameMsgCallback&& cbk);
        void SetOnAudioFrameMsgCallback(OnAudioFrameMsgCallback&& cbk);
        void SetOnCursorInfoSyncMsgCallback(OnCursorInfoSyncMsgCallback&& cbk);
        void SetOnAudioSpectrumCallback(OnAudioSpectrumCallback&& cbk);
        void SetOnConnectCallback(OnConnectedCallback&& cbk);
        void SetOnDisconnectedCallback(OnDisconnectedCallback&& cbk);
        void SetOnHeartBeatCallback(OnHeartBeatInfoCallback&& cbk);
        void SetOnClipboardCallback(OnClipboardInfoCallback&& cbk);
        void SetOnServerConfigurationCallback(OnConfigCallback&& cbk);
        void SetOnMonitorSwitchedCallback(OnMonitorSwitchedCallback&& cbk);
        void SetOnRawMessageCallback(OnRawMessageCallback&& cbk);

        // decoded video frames(packed I420) from the webrtc local(direct) connection
        using OnRtcLocalVideoFrameCallback = std::function<void(int w, int h, std::shared_ptr<Data> i420)>;
        void SetOnRtcLocalVideoFrameCallback(OnRtcLocalVideoFrameCallback&& cbk);

        // decoded audio(16-bit interleaved PCM) from the webrtc local(direct) connection
        using OnRtcLocalAudioCallback = std::function<void(std::shared_ptr<Data> pcm, int sample_rate, int channels)>;
        void SetOnRtcLocalAudioCallback(OnRtcLocalAudioCallback&& cbk);

        // rtc local encoded-sink mode, old-render compat: current capturing monitor
        // name(from ServerConfiguration) for the single dynamic track
        void SetRtcLocalCapturingMonitorNameProvider(std::function<std::string()>&& provider);

        int64_t GetQueuingMediaMsgCount();
        int64_t GetQueuingFtMsgCount();

        void On16msTimeout();

        // retry connection
        void RetryConnection();

    private:
        std::shared_ptr<tc::Message> ParseMessage(std::shared_ptr<Data> msg);
        void HeartBeat();

    private:
        std::shared_ptr<Connection> media_conn_ = nullptr;
        std::shared_ptr<Connection> ft_conn_ = nullptr;
        std::shared_ptr<WebRtcConnection> rtc_conn_ = nullptr;
        std::shared_ptr<WebRtcLocalConnection> rtc_local_conn_ = nullptr;
        std::shared_ptr<UdpDirectConnection> udp_direct_conn_ = nullptr;
        OnRtcLocalVideoFrameCallback rtc_local_video_frame_cbk_;
        OnRtcLocalAudioCallback rtc_local_audio_cbk_;
        OnVideoFrameMsgCallback video_frame_cbk_;
        OnAudioFrameMsgCallback audio_frame_cbk_;
        OnCursorInfoSyncMsgCallback cursor_info_sync_cbk_;
        OnAudioSpectrumCallback audio_spectrum_cbk_;
        OnConnectedCallback conn_cbk_;
        OnDisconnectedCallback dis_conn_cbk_;
        OnHeartBeatInfoCallback hb_cbk_;
        OnClipboardInfoCallback clipboard_cbk_;
        OnConfigCallback config_cbk_;
        OnMonitorSwitchedCallback monitor_switched_cbk_;
        OnRawMessageCallback raw_msg_cbk_;

        std::shared_ptr<ThunderSdkParams> sdk_params_;

        std::string media_path_{};
        std::string ft_path_;
        ClientNetworkType network_type_;
        std::string device_id_;
        std::string remote_device_id_;
        std::string ft_device_id_;
        std::string ft_remote_device_id_;
        std::string stream_id_;

        std::atomic_int queuing_message_count_ = 0;
        uint64_t hb_idx_ = 0;

        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;

        SdkStatistics* stat_ = nullptr;
    };

}

#endif //TC_CLIENT_PC_WS_CLIENT_H
