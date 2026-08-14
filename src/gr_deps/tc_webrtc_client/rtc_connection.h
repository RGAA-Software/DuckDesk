//
// Created by RGAA on 13/04/2025.
//

#ifndef GAMMARAY_RTC_CONNECTION_H
#define GAMMARAY_RTC_CONNECTION_H

#include "rtc_client_interface.h"
#include "tc_common_new/webrtc_helper.h"
#include <memory>
#include <mutex>
#include <vector>

extern "C" __declspec(dllexport) void* GetInstance();

namespace tc
{

    class PeerCallback;
    class SetSessCallback;
    class CreateSessCallback;
    class RtcDataChannel;
    class RtcVideoSink;
    class RtcEncodedFrameSink;
    class RtcAudioSink;
    class Thread;

    // cached ice
    class CachedIce {
    public:
        std::string ice_;
        std::string mid_;
        int sdp_mline_index_{0};
    };

    class RtcConnection : public RtcClientInterface {
    public:
        RtcConnection();
        bool Init(const std::string& remote_device_id) override;
        bool Exit() override;
        bool OnRemoteSdp(const std::string &sdp) override;
        bool OnRemoteIce(const std::string &ice, const std::string &mid, int32_t sdp_mline_index) override;

        void PostMediaMessage(std::shared_ptr<Data> msg) override;
        void PostFtMessage(std::shared_ptr<Data> msg) override;
        void PostInputMessage(std::shared_ptr<Data> msg) override;

        int64_t GetQueuingMediaMsgCount() override;
        int64_t GetQueuingFtMsgCount() override;

        bool HasEnoughBufferForQueuingMediaMessages() override;
        bool HasEnoughBufferForQueuingFtMessages() override;

        bool IsMediaChannelReady() override;
        bool IsFtChannelReady() override;
        bool IsInputChannelReady() override;

        void On16msTimeout() override;

        void SetLocalRtcMode(bool on) override;

        // called by PeerCallback
        void OnIceGatheringComplete();
        void OnVideoTrack(webrtc::VideoTrackInterface* track);
        void OnAudioTrack(webrtc::AudioTrackInterface* track);
        void OnIceStateChanged(int state);

        void PostWorkTask(std::function<void()>&& task);

    private:
        void CreatePeerConnection();
        void CreatePeerConnectionFactory();

        void SendCachedIces();

    private:
        std::shared_ptr<PeerCallback> peer_callback_ = nullptr;
        rtc::scoped_refptr<SetSessCallback> set_local_sdp_callback_ = nullptr;
        rtc::scoped_refptr<SetSessCallback> set_remote_sdp_callback_ = nullptr;
        rtc::scoped_refptr<CreateSessCallback> create_sess_callback_ = nullptr;

        std::unique_ptr<rtc::Thread> network_thread_;
        std::unique_ptr<rtc::Thread> worker_thread_;
        std::unique_ptr<rtc::Thread> sig_thread_;

        rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_conn_ = nullptr;
        rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_conn_factory_;
        webrtc::PeerConnectionInterface::RTCConfiguration configuration_;
        std::shared_ptr<RtcDataChannel> media_data_channel_ = nullptr;
        std::shared_ptr<RtcDataChannel> ft_data_channel_ = nullptr;
        std::shared_ptr<RtcDataChannel> input_data_channel_ = nullptr;

        std::string local_sdp_;
        std::mutex ice_mtx_;
        std::vector<CachedIce> cached_ices_;
        std::atomic_bool already_set_answer_sdp_ = false;

        std::shared_ptr<Thread> work_thread_ = nullptr;

        // local(direct) mode: no STUN server, non-trickle signaling
        bool local_rtc_mode_ = false;
        std::shared_ptr<RtcVideoSink> video_sink_ = nullptr;

        // encoded-sink mode(local multi-track): one sink per remote video track,
        // consuming pre-decode H264; the built-in decoder is replaced by a null one
        std::vector<std::shared_ptr<RtcEncodedFrameSink>> encoded_sinks_;
        int video_track_arrive_count_ = 0;

        // audio sink(local mode): taps decoded PCM from the remote audio track,
        // the sdk's own AudioPlayer plays it(the dll's ADM is a dummy)
        std::shared_ptr<RtcAudioSink> audio_sink_;

    };

}

#endif //GAMMARAY_RTC_CONNECTION_H
