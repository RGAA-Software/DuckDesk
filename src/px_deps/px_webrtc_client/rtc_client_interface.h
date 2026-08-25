//
// Created by RGAA on 13/04/2025.
//

#ifndef PX_RTC_CLIENT_INTERFACE_H
#define PX_RTC_CLIENT_INTERFACE_H

#include <memory>
#include <string>
#include <functional>

namespace px
{

    class Data;

    using OnLocalSdpSetCallback = std::function<void(const std::string&)>;
    using OnLocalIceCallback = std::function<void(const std::string& ice, const std::string& mid, int sdp_mline_index)>;

    using OnMediaMessageCallback = std::function<void(std::shared_ptr<Data>)>;
    using OnFtMessageCallback = std::function<void(std::shared_ptr<Data>)>;

    // decoded video frame, packed I420
    using OnVideoFrameCallback = std::function<void(int w, int h, std::shared_ptr<Data> i420)>;
    // encoded(pre-decode) video frame, H264 AnnexB. track_index = video track order
    // in the answer(0-based), matching the "monitors" array from the signaling response.
    using OnEncodedVideoFrameCallback = std::function<void(int track_index, bool key, int w, int h, std::shared_ptr<Data> encoded)>;
    // decoded audio from the remote audio track: 16-bit interleaved PCM.
    // the dll runs a dummy ADM(no real playout), the sdk feeds its own AudioPlayer.
    using OnAudioDataCallback = std::function<void(std::shared_ptr<Data> pcm, int sample_rate, int channels)>;
    // webrtc::PeerConnectionInterface::IceConnectionState, passed as int to keep webrtc types out of this header
    using OnIceStateCallback = std::function<void(int state)>;
    // Sanitized by the SDK before display/logging. The DLL only returns the
    // libwebrtc stats report in-memory; TURN credentials are never part of it.
    using OnStatsJsonCallback = std::function<void(const std::string& json)>;

    class RtcClientInterface {
    public:
        virtual bool Init(const std::string& remote_device_id) {
            return false;
        }

        virtual bool Exit() {
            return false;
        }

        virtual bool OnRemoteSdp(const std::string& sdp) {
            return false;
        }

        virtual bool OnRemoteIce(const std::string& ice, const std::string& mid, int32_t sdp_mline_index) {
            return false;
        }

        virtual void SetOnLocalSdpSetCallback(OnLocalSdpSetCallback&& cbk) {
            local_sdp_set_cbk_ = cbk;
        }

        virtual void SetOnLocalIceCallback(OnLocalIceCallback&& cbk) {
            local_ice_cbk_ = cbk;
        }

        virtual void PostMediaMessage(std::shared_ptr<Data> msg) = 0;
        virtual void PostFtMessage(std::shared_ptr<Data> msg) = 0;
        // dedicated input channel(reliable + ordered, keyboard/mouse only)
        virtual void PostInputMessage(std::shared_ptr<Data> msg) {}

        virtual void SetMediaMessageCallback(const OnMediaMessageCallback& cbk) {
            media_msg_cbk_ = cbk;
        }

        virtual void SetFtMessageCallback(const OnFtMessageCallback& cbk) {
            ft_msg_cbk_ = cbk;
        }

        virtual int64_t GetQueuingMediaMsgCount() = 0;
        virtual int64_t GetQueuingFtMsgCount() = 0;

        virtual bool HasEnoughBufferForQueuingMediaMessages() = 0;
        virtual bool HasEnoughBufferForQueuingFtMessages() = 0;

        virtual bool IsMediaChannelReady() = 0;
        virtual bool IsFtChannelReady() = 0;
        virtual bool IsInputChannelReady() { return false; }

        virtual void On16msTimeout() {}

        // local(direct) mode: no STUN server, non-trickle signaling,
        // the final offer sdp(with candidates embedded) is reported after ice gathering complete.
        // must be called before Init()
        virtual void SetLocalRtcMode(bool on) {}

        // Serialized RtcSessionIceConfig. Must be set before Init(); secrets
        // are consumed in-memory and never logged by the RTC DLL.
        virtual void SetIceServersJson(const std::string& json) {}

        // Apply a fresh Console-issued ICE snapshot to the active
        // PeerConnection and create an ICE-restart offer. The normal local SDP
        // callback carries that offer over the existing signaling room.
        virtual bool RestartIce(const std::string& json) { return false; }

        // A standalone file manager negotiates only the reliable FT data
        // channel: no media/input data channels and no audio/video m-lines.
        // Must be called before Init().
        virtual void SetFileTransferOnly(bool on) {
            file_transfer_only_ = on;
        }

        // how many video m-lines the offer declares(recvonly). must be called before Init().
        // >1 asks the render for one video track per monitor; an old render simply
        // answers a single track, the extra m-lines stay inactive.
        virtual void SetVideoTrackCount(int count) {
            video_track_count_ = count;
        }

        // when set(in local mode), video tracks are consumed as ENCODED frames via
        // AddEncodedSink(pre-decode H264) instead of the built-in decoder + I420 sink.
        virtual void SetOnEncodedVideoFrameCallback(OnEncodedVideoFrameCallback&& cbk) {
            encoded_video_frame_cbk_ = cbk;
        }

        virtual void SetOnVideoFrameCallback(OnVideoFrameCallback&& cbk) {
            video_frame_cbk_ = cbk;
        }

        // When set, the remote audio track is tapped for decoded PCM via an
        // AudioSinkInterface instead of WebRTC's dummy playout.
        virtual void SetOnAudioDataCallback(OnAudioDataCallback&& cbk) {
            audio_data_cbk_ = cbk;
        }

        virtual void SetOnIceStateCallback(OnIceStateCallback&& cbk) {
            ice_state_cbk_ = cbk;
        }

        virtual void SetOnStatsJsonCallback(OnStatsJsonCallback&& cbk) {
            stats_json_cbk_ = cbk;
        }

    protected:
        std::string remote_device_id_;

        OnLocalSdpSetCallback local_sdp_set_cbk_;
        OnLocalIceCallback local_ice_cbk_;

        OnMediaMessageCallback media_msg_cbk_;
        OnFtMessageCallback ft_msg_cbk_;

        OnVideoFrameCallback video_frame_cbk_;
        OnIceStateCallback ice_state_cbk_;
        OnStatsJsonCallback stats_json_cbk_;

        int video_track_count_ = 1;
        bool file_transfer_only_ = false;
        OnEncodedVideoFrameCallback encoded_video_frame_cbk_;
        OnAudioDataCallback audio_data_cbk_;
    };

}

#endif //PX_RTC_CLIENT_INTERFACE_H
