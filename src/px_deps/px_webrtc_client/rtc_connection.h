//
// Created by RGAA on 13/04/2025.
//

#ifndef PX_RTC_CONNECTION_H
#define PX_RTC_CONNECTION_H

#include "rtc_client.h"
#include "px_common_new/webrtc_helper.h"
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace px {

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

class RtcConnection final {
  public:
    RtcConnection();
    ~RtcConnection();
    bool Init(const std::string& remote_device_id);
    bool Exit();
    bool OnRemoteSdp(const std::string& sdp);
    bool OnRemoteIce(const std::string& ice, const std::string& mid, int32_t sdp_mline_index);

    void PostMediaMessage(std::shared_ptr<Data> msg);
    void PostFtMessage(std::shared_ptr<Data> msg);
    void PostInputMessage(std::shared_ptr<Data> msg);

    int64_t GetQueuingMediaMsgCount();
    int64_t GetQueuingFtMsgCount();

    bool HasEnoughBufferForQueuingMediaMessages();
    bool HasEnoughBufferForQueuingFtMessages();

    bool IsMediaChannelReady();
    bool IsFtChannelReady();
    bool IsInputChannelReady();

    void On16msTimeout();

    void SetLocalRtcMode(bool on);
    void SetIceServersJson(const std::string& json);
    bool RestartIce(const std::string& json);

    void SetOnLocalSdpSetCallback(OnLocalSdpSetCallback callback) {
        local_sdp_set_cbk_ = std::move(callback);
    }
    void SetOnLocalIceCallback(OnLocalIceCallback callback) {
        local_ice_cbk_ = std::move(callback);
    }
    void SetMediaMessageCallback(OnMediaMessageCallback callback) {
        media_msg_cbk_ = std::move(callback);
    }
    void SetFtMessageCallback(OnFtMessageCallback callback) {
        ft_msg_cbk_ = std::move(callback);
    }
    void SetFileTransferOnly(bool enabled) {
        file_transfer_only_ = enabled;
    }
    void SetVideoTrackCount(int count) {
        video_track_count_ = count;
    }
    void SetOnEncodedVideoFrameCallback(OnEncodedVideoFrameCallback callback) {
        encoded_video_frame_cbk_ = std::move(callback);
    }
    void SetOnVideoFrameCallback(OnVideoFrameCallback callback) {
        video_frame_cbk_ = std::move(callback);
    }
    void SetOnAudioDataCallback(OnAudioDataCallback callback) {
        audio_data_cbk_ = std::move(callback);
    }
    void SetOnIceStateCallback(OnIceStateCallback callback) {
        ice_state_cbk_ = std::move(callback);
    }
    void SetOnStatsJsonCallback(OnStatsJsonCallback callback) {
        stats_json_cbk_ = std::move(callback);
    }

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
    bool ApplyIceServersJson(const std::string& json, bool active);
    void RequestStats();

  private:
    std::string remote_device_id_;
    OnLocalSdpSetCallback local_sdp_set_cbk_;
    OnLocalIceCallback local_ice_cbk_;
    OnMediaMessageCallback media_msg_cbk_;
    OnFtMessageCallback ft_msg_cbk_;
    OnVideoFrameCallback video_frame_cbk_;
    OnIceStateCallback ice_state_cbk_;
    OnStatsJsonCallback stats_json_cbk_;
    OnEncodedVideoFrameCallback encoded_video_frame_cbk_;
    OnAudioDataCallback audio_data_cbk_;
    int video_track_count_ = 1;
    bool file_transfer_only_ = false;

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
    std::string ice_servers_json_;
    std::shared_ptr<RtcVideoSink> video_sink_ = nullptr;

    // encoded-sink mode(local multi-track): one sink per remote video track,
    // consuming pre-decode H264; the built-in decoder is replaced by a null one
    std::vector<std::shared_ptr<RtcEncodedFrameSink>> encoded_sinks_;
    int video_track_arrive_count_ = 0;

    // audio sink(local mode): taps decoded PCM from the remote audio track,
    // the sdk's own AudioPlayer plays it(the dll's ADM is a dummy)
    std::shared_ptr<RtcAudioSink> audio_sink_;
    uint32_t stats_tick_ = 0;
};

} // namespace px

#endif // PX_RTC_CONNECTION_H
