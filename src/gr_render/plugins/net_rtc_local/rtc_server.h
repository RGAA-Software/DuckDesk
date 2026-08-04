//
// Created by hy on 2024/4/25.
//

#ifndef TEST_WEBRTC_RTCSERVER_H
#define TEST_WEBRTC_RTCSERVER_H

#include "tc_common_new/webrtc_helper.h"
#include "gr_render/plugin_interface/gr_plugin_interface.h"

namespace tc
{
    class Data;
    class PeerCallback;
    class CreateSessCallback;
    class SetSessCallback;
    class RtcLocalPlugin;
    class RtcDataChannel;
    class AudioSourceImpl;
    class VideoSourceImpl;
    class VideoTrackSourceImpl;
    class RemoteAudioSink;

    class RtcServer : public std::enable_shared_from_this<RtcServer> {
    public:
        static std::shared_ptr<RtcServer> Make(RtcLocalPlugin* plugin);
        explicit RtcServer(RtcLocalPlugin* plugin);
        RtcLocalPlugin* GetPlugin();

        bool Start(const std::string& stream_id, const std::string& offer_sdp);
        void Exit();
        void OnRemoteIce(const std::string& ice, const std::string& mid, int sdp_mline_index);
        bool IsDataChannelConnected();
        bool IsFtDataChannelConnected();

        // conn_id: rtc_servers_ 的 map key(device_id:stream_id),断开清理时回传给 plugin
        void SetConnId(const std::string& conn_id) { conn_id_ = conn_id; }
        // 请求退出:置 exit_ 标记,停止一切收发;真正的资源回收由 plugin 延迟 Sweep
        void RequestExit() { exit_ = true; }
        bool IsExitRequested() const { return exit_.load(); }

        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through = false);
        bool PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through = false);
        bool PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through = false);

        uint32_t GetMediaPendingMessages();
        uint32_t GetFtPendingMessages();

        bool HasEnoughBufferForQueuingMediaMessages();
        bool HasEnoughBufferForQueuingFtMessages();

        void On100msTimeout();

        std::string GetAnswerSdp();
        void SetOnAnswerCallback(std::function<void(const std::string& answer_sdp)>&& callback);

        void OnNewFrameCaptured(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format);

    private:
        void CreatePeerConnectionFactory();
        void CreatePeerConnection();
        void CreateSomeMediaDeps(webrtc::PeerConnectionFactoryDependencies& media_deps);

        void SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index);

        // 远端音频轨(浏览器麦克风上行):挂 PCM sink,经 WASAPI 播放
        void OnRemoteAudioTrack(rtc::scoped_refptr<webrtc::AudioTrackInterface> track);
        void OnRemoteAudioTrackRemoved(rtc::scoped_refptr<webrtc::AudioTrackInterface> track);

    private:
        RtcLocalPlugin* plugin_ = nullptr;
        std::unique_ptr<rtc::Thread> network_thread_;
        std::unique_ptr<rtc::Thread> worker_thread_;
        std::unique_ptr<rtc::Thread> sig_thread_;
        std::string stream_id_;
        std::string offer_sdp_;
        std::string answer_sdp_;
        std::shared_ptr<PeerCallback> peer_callback_ = nullptr;
        rtc::scoped_refptr<SetSessCallback> set_remote_offer_sdp_callback_ = nullptr;
        rtc::scoped_refptr<SetSessCallback> set_local_answer_sdp_callback_ = nullptr;
        rtc::scoped_refptr<CreateSessCallback> create_answer_callback_ = nullptr;

        rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_conn_ = nullptr;
        rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_conn_factory_;
        webrtc::PeerConnectionInterface::RTCConfiguration configuration_;

        std::shared_ptr<RtcDataChannel> media_data_channel_ = nullptr;
        std::shared_ptr<RtcDataChannel> ft_data_channel_ = nullptr;
        std::atomic<bool> exit_ = false;
        // Exit 幂等标记:ICE 终态回调/插件 Sweep/takeover 替换都可能触发 Exit
        std::atomic<bool> cleaned_up_ = false;
        std::string conn_id_;
        std::function<void(const std::string& answer_sdp)> answer_sdp_callback_;

        std::shared_ptr<VideoSourceImpl> video_source_ = nullptr;
        rtc::scoped_refptr<VideoTrackSourceImpl> video_track_source_ = nullptr;
        rtc::scoped_refptr<AudioSourceImpl> audio_source_ = nullptr;

        // 上行音频(浏览器麦克风):接收统计 sink;播放由 libwebrtc 默认 ADM 完成
        rtc::scoped_refptr<webrtc::AudioTrackInterface> remote_audio_track_ = nullptr;
        std::shared_ptr<RemoteAudioSink> remote_audio_sink_ = nullptr;

        // last captured frame index
        uint64_t last_captured_frame_index_ = 0;
    };

}

#endif //TEST_WEBRTC_RTCSERVER_H
